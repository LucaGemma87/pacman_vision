#include <pacman_vision/tracker.h>

//Constructor
Tracker::Tracker(ros::NodeHandle &n, boost::shared_ptr<Storage> &stor)
{
    this->scene.reset(new PXC);
    this->model.reset(new PXC);
    this->orig_model.reset(new PXC);
    this->nh = ros::NodeHandle (n, "tracker");
    this->queue_ptr.reset(new ros::CallbackQueue);
    this->nh.setCallbackQueue(&(*this->queue_ptr));
    this->storage = stor;
    this->srv_track_object = nh.advertiseService("track_object",
                                            &Tracker::cb_track_object, this);
    this->srv_stop = nh.advertiseService("stop_track",
                                            &Tracker::cb_stop_tracker, this);
    this->srv_grasp = nh.advertiseService("grasp_verification",
                                                    &Tracker::cb_grasp, this);
    this->bounding_box.reset(new Box);
    this->bounding_box_original.reset(new Box);

    nh.param<int>("/pacman_vision/estimation_type", type, 0);
    ce.reset( new pcl::registration::CorrespondenceEstimation<PX, PX, float>);
    crd.reset( new pcl::registration::CorrespondenceRejectorDistance);
    crt.reset( new pcl::registration::CorrespondenceRejectorTrimmed);
    cro2o.reset( new pcl::registration::CorrespondenceRejectorOneToOne);
    teDQ.reset(new pcl::registration::TransformationEstimationDualQuaternion
                                                                <PX,PX,float>);
    // crsc.reset( new pcl::registration::CorrespondenceRejectorSampleConsensus<PX>);

    started = lost_it = false;
    leaf = 0.02f;
    factor = 1.5f;
    rej_distance = 0.025f;
    index = -1;
    manual_disturbance = false;
    disturbance_counter= centroid_counter = error_count = disturbance_done = 0;
}

Tracker::~Tracker()
{
    this->nh.shutdown();
}

//tracker step
void
Tracker::track()
{
    this->storage->read_scene_processed(scene);
    if (error_count >= 30)
    {
        //failed 30 times in a row
        ROS_ERROR("[Tracker][%s] Object is lost ... stopping tracker..."
                                                                    ,__func__);
        started = false;
        lost_it = true;
        return;
    }
    Eigen::Matrix4f inv_trans;
    PXC::Ptr target (new PXC);
    PXC::Ptr tmp (new PXC);
    inv_trans = transform->inverse();
    //BoundingBox
    bounding_box->x1 = factor * bounding_box_original->x1;
    bounding_box->x2 = factor * bounding_box_original->x2;
    bounding_box->y1 = factor * bounding_box_original->y1;
    bounding_box->y2 = factor * bounding_box_original->y2;
    /*
     * zmin would certainly be positive, due to model, we need it negative
     * TODO: add a check when zmin gets computed
     */
    bounding_box->z1 = - factor * bounding_box_original->z1;
    bounding_box->z2 = factor * bounding_box_original->z2;
    pcl::transformPointCloud(*scene, *tmp, inv_trans);
    pass.setInputCloud(tmp);
    pass.setFilterFieldName("x");
    pass.setFilterLimits(bounding_box->x1, bounding_box->x2);
    pass.filter(*tmp);
    pass.setInputCloud(tmp);
    pass.setFilterFieldName("y");
    pass.setFilterLimits(bounding_box->y1, bounding_box->y2);
    pass.filter(*tmp);
    pass.setInputCloud(tmp);
    pass.setFilterFieldName("z");
    pass.setFilterLimits(bounding_box->z1, bounding_box->z2);
    pass.filter(*tmp);
    //Also save bounding box to Storage, so that Broadcaster can publish it
    this->storage->write_tracked_box(bounding_box);
    if (tmp->points.size() <= 30)
    {
        ROS_ERROR("[Tracker][%s] Not enought points in bounding box, retryng with larger bounding box"
                                                                    ,__func__);
        factor += 0.5;
        rej_distance +=0.005;
        ++error_count;
        return;
    }
    pcl::transformPointCloud(*tmp, *target, *transform);
    /*
     *  Alignment
     */

    //check if user changed leaf size
    if (old_leaf != leaf)
    {
        vg.setInputCloud (orig_model);
        vg.setLeafSize(leaf, leaf, leaf);
        vg.filter (*model);
        old_leaf = leaf;
        ce->setInputSource(model);
        icp.setInputSource(model);
        pcl::CentroidPoint<PX> mc;
        for (int i=0; i<model->points.size(); ++i)
            mc.add(model->points[i]);
        mc.get(model_centroid);
    }
    pcl::Correspondences corr, final_corr;
    ce->setInputTarget(target);
    ce->determineCorrespondences(corr);
    crd->setMaximumDistance(rej_distance);
    //crt->getRemainingCorrespondences(corr, interm);
    crd->getRemainingCorrespondences(corr, final_corr);
    //cro2o->getRemainingCorrespondences(interm, filt);
    //crsc->setInputTarget(target);
    icp.setInputTarget(target);
    if (centroid_counter >=5)
    {
        pcl::CentroidPoint<PX> tc;
        for (int i=0; i<target->points.size(); ++i)
            tc.add(target->points[i]);
        PX target_centroid, mc_transformed;
        mc_transformed = pcl::transformPoint(model_centroid,
                                                Eigen::Affine3f(*transform));
        tc.get(target_centroid);
        Eigen::Matrix4f Tcen, guess;
        Tcen <<  1, 0, 0,  (target_centroid.x - mc_transformed.x),
                 0, 1, 0,  (target_centroid.y - mc_transformed.y),
                 0, 0, 1,  (target_centroid.z - mc_transformed.z),
                 0, 0, 0,  1;
        guess = Tcen*(*transform);
        icp.align(*tmp, guess);
        centroid_counter = 0;
        ROS_WARN("[Tracker][%s] Centroid Translation Performed!",__func__);
        if (icp.getFitnessScore() < 0.001 )
        {
            fitness = icp.getFitnessScore();
            *(this->transform) = icp.getFinalTransformation();
        }
    }
    else if (disturbance_counter >= 10 || manual_disturbance)
    {
        boost::random::mt19937 gen(std::time(0));
        boost::random::uniform_int_distribution<> angle(20,90);
        float angx,angy,angz;
        if (angle(gen)%2)
            angx = -D2R*angle(gen);
        else
            angx = D2R*angle(gen);
        Eigen::AngleAxisf rotx (angx, Eigen::Vector3f::UnitX());
        T_rotx<<rotx.matrix()(0,0), rotx.matrix()(0,1), rotx.matrix()(0,2), 0,
                rotx.matrix()(1,0), rotx.matrix()(1,1), rotx.matrix()(1,2), 0,
                rotx.matrix()(2,0), rotx.matrix()(2,1), rotx.matrix()(2,2), 0,
                0,                  0,                  0,                  1;
        if (angle(gen)%2)
            angz = -D2R*angle(gen);
        else
            angz = D2R*angle(gen);
        Eigen::AngleAxisf rotz (angz, Eigen::Vector3f::UnitZ());
        T_rotz<<rotz.matrix()(0,0), rotz.matrix()(0,1), rotz.matrix()(0,2), 0,
                rotz.matrix()(1,0), rotz.matrix()(1,1), rotz.matrix()(1,2), 0,
                rotz.matrix()(2,0), rotz.matrix()(2,1), rotz.matrix()(2,2), 0,
                0,                  0,                  0,                  1;
        /*
           if (angle(gen)%2)
           angy = -D2R*angle(gen);
           else
           angy = D2R*angle(gen);
           Eigen::AngleAxisf roty (angy, Eigen::Vector3f::UnitY());
           T_roty << roty.matrix()(0,0), roty.matrix()(0,1), roty.matrix()(0,2), 0,
           roty.matrix()(1,0), roty.matrix()(1,1), roty.matrix()(1,2), 0,
           roty.matrix()(2,0), roty.matrix()(2,1), roty.matrix()(2,2), 0,
           0,                0,                  0,                 1;
           */
        Eigen::Matrix4f disturbed;
        disturbed = (T_rotz*T_rotx*inv_trans).inverse();
        if (disturbance_done >5 || manual_disturbance)
        {
            manual_disturbance = false;
            ROS_WARN("[Tracker][%s] Triggered Heavy Disturbance!",__func__);
            Eigen::AngleAxisf rothz (3.14159/2, Eigen::Vector3f::UnitZ());
            Eigen::AngleAxisf rothx (3.14159/2, Eigen::Vector3f::UnitX());
            Eigen::AngleAxisf rothy (3.14159/2, Eigen::Vector3f::UnitY());
            Eigen::Matrix4f Tx,Tz,Ty;
            if (angle(gen)%2)
            {
                Tx<<rothx.matrix()(0,0), rothx.matrix()(0,1), rothx.matrix()(0,2), 0,
                    rothx.matrix()(1,0), rothx.matrix()(1,1), rothx.matrix()(1,2), 0,
                    rothx.matrix()(2,0), rothx.matrix()(2,1), rothx.matrix()(2,2), 0,
                    0,                0,                  0,                 1;
                disturbed = (Tx*inv_trans).inverse();
            }
            else if (angle(gen)%3)
            {
                Ty<<rothy.matrix()(0,0), rothy.matrix()(0,1), rothy.matrix()(0,2), 0,
                    rothy.matrix()(1,0), rothy.matrix()(1,1), rothy.matrix()(1,2), 0,
                    rothy.matrix()(2,0), rothy.matrix()(2,1), rothy.matrix()(2,2), 0,
                    0,                0,                  0,                 1;
                disturbed = (Ty*inv_trans).inverse();
            }
            else
            {
                Tz<<rothz.matrix()(0,0), rothz.matrix()(0,1), rothz.matrix()(0,2), 0,
                    rothz.matrix()(1,0), rothz.matrix()(1,1), rothz.matrix()(1,2), 0,
                    rothz.matrix()(2,0), rothz.matrix()(2,1), rothz.matrix()(2,2), 0,
                    0,                0,                  0,                 1;
                disturbed = (Tz*inv_trans).inverse();
            }
            disturbance_done = 0;
        }
        else
            ROS_WARN("[Tracker][%s] Triggered Disturbance! With angles %g, %g"
                                            ,__func__,  angx/D2R, angz/D2R);
        icp.align(*tmp, disturbed);
        ++disturbance_done;
        disturbance_counter = 0;
        if (icp.getFitnessScore() < 0.001 )
        {
            fitness = icp.getFitnessScore();
            *(this->transform) = icp.getFinalTransformation();
        }
    }
    else
    {
        icp.align(*tmp, *transform);
        fitness = icp.getFitnessScore();
        *(this->transform) = icp.getFinalTransformation();
    }
    //ROS_INFO("corr: %d, final_corr: %d, fitness: %g", (int)corr.size(),
    //                                        (int)final_corr.size(), fitness);
    this->storage->write_obj_transform_by_index(index, this->transform);
    //adjust distance and factor according to fitness
    if (fitness > 0.0008 ) //something is probably wrong
    {
        rej_distance +=0.001;
        factor += 0.05;
        if (rej_distance > 2.0)
            rej_distance = 2.0;
        if (factor > 5.0)
            factor = 5.0;
        ++disturbance_counter;
        ++centroid_counter;
        return;
    }
    else if (fitness < 0.0005) //all looks good, lower factors and distances
    {
        rej_distance -=0.005;
        if(rej_distance < 0.025)
            rej_distance = 0.025; //we dont want to go lower than this
        factor -=0.05;
        if(factor < 1.3)
            factor = 1.3;
    }
    error_count = 0;
    disturbance_counter = 0;
    centroid_counter = 0;
    disturbance_done = 0;
}

void
Tracker::find_object_in_scene()
{
    this->storage->read_scene_processed(scene);
    if (scene->points.size() > model->points.size()/3)
    {
        pcl::CentroidPoint<PX> tc;
        for (int i=0; i<scene->points.size(); ++i)
            tc.add(scene->points[i]);
        PX target_centroid, mc_transformed;
        mc_transformed = pcl::transformPoint(model_centroid,
                                                Eigen::Affine3f(*transform));
        tc.get(target_centroid);
        Eigen::Matrix4f Tcen, guess;
        Tcen<<  1, 0, 0,  (target_centroid.x - mc_transformed.x),
                0, 1, 0,  (target_centroid.y - mc_transformed.y),
                0, 0, 1,  (target_centroid.z - mc_transformed.z),
                0, 0, 0,  1;
        guess = Tcen*(*transform);
        *transform = guess;
        this->started = true;
        this->lost_it = false;
        this->disturbance_counter = 0;
        this->error_count = 0;
        this->centroid_counter = 0;
        ROS_INFO("[Tracker][%s] Found something that could be the object, trying to track that"
                                                                    ,__func__);
        return;
    }
    else
    {
        ROS_WARN("[Tracker][%s] Nothing is found on scene yet...",__func__);
        return;
    }
}

bool
Tracker::cb_track_object(pacman_vision_comm::track_object::Request& req,
                            pacman_vision_comm::track_object::Response& res)
{
    if (req.name.empty())
    {
        ROS_ERROR("[Tracker][%s] You need to provide the name of the object you want to track!"
                                                                , __func__);
        return false;
    }
    std::string models_path (ros::package::getPath("asus_scanner_models"));
    if ( !this->storage->search_obj_name(req.name, index) )
    {
        ROS_ERROR("[Tracker][%s] Cannot find %s from the pool of already estimated objects, check spelling or run an estimation first!"
                                                , __func__, req.name.c_str());
        return false;
    }
    name = (req.name);
    std::vector<std::string> vst;
    boost::split(vst, name, boost::is_any_of("_"), boost::token_compress_on);
    id = vst.at(0);
    if ( !this->storage->read_obj_transform_by_index(index, transform) )
    {
        ROS_ERROR("[Tracker][%s] Cannot find %s transform from the pool of already estimated transforms, check spelling or run an estimation first!"
                                                , __func__, req.name.c_str());
        return false;
    }
    boost::filesystem::path model_path(models_path+ "/"+ id+ "/"+ id+ ".pcd");
    if (boost::filesystem::exists(model_path) &&
                                boost::filesystem::is_regular_file(model_path))
    {
        if (pcl::io::loadPCDFile(model_path.c_str(), *orig_model))
        {
            ROS_ERROR("[Tracker][%s] Error loading model %s"
                                                ,__func__, model_path.c_str());
            return false;
        }
    }
    else
    {
        ROS_ERROR("[Tracker][%s] Requested model (%s) does not exists in asus_scanner_models package"
                                        ,__func__, model_path.stem().c_str());
        return false;
    }
    old_leaf = leaf;
    vg.setInputCloud (orig_model);
    vg.setLeafSize(leaf, leaf, leaf);
    vg.filter (*model);
    /*
     * Get the minimum and maximum values on each of the 3 (x-y-z)
     * dimensions of model, also get model centroid
     */
    pcl::CentroidPoint<PX> mc;
    std::vector<float> xvec,yvec,zvec;
    for (int i=0; i<model->points.size(); ++i)
    {
        xvec.push_back(model->points[i].x);
        yvec.push_back(model->points[i].y);
        zvec.push_back(model->points[i].z);
        mc.add(model->points[i]);
    }
    bounding_box_original->x1 = *std::min_element(xvec.begin(), xvec.end());
    bounding_box_original->y1 = *std::min_element(yvec.begin(), yvec.end());
    bounding_box_original->z1 = *std::min_element(zvec.begin(), zvec.end());
    bounding_box_original->x2 = *std::max_element(xvec.begin(), xvec.end());
    bounding_box_original->y2 = *std::max_element(yvec.begin(), yvec.end());
    bounding_box_original->z2 = *std::max_element(zvec.begin(), zvec.end());
    mc.get(model_centroid);

    //init icps
    icp.setUseReciprocalCorrespondences(false);
    icp.setMaximumIterations(50);
    icp.setTransformationEpsilon(1e-9);
    icp.setEuclideanFitnessEpsilon(1e-9);
    //Correspondence Estimator
    ce->setInputSource(model);
    icp.setCorrespondenceEstimation(ce);
    //Correspondence Rejector(s)
    crd->setMaximumDistance(rej_distance);
    //crsc->setInputSource(model);
    //crsc->setInlierThreshold(0.02);
    //crsc->setMaximumIterations(5);
    //crsc->setRefineModel(true);
    // crt->setOverlapRatio(1);
    // crt->setMinCorrespondences(200);
    // icp.addCorrespondenceRejector(crt);
    icp.addCorrespondenceRejector(crd);
    //icp.addCorrespondenceRejector(cro2o);
    icp.setInputSource(model);
    //Transformation Estimation
    icp.setTransformationEstimation(teDQ);

    this->storage->write_tracked_index(index);
    //we are ready to start
    started = true;
    return true;
}

bool
Tracker::cb_stop_tracker(pacman_vision_comm::stop_track::Request& req,
                                pacman_vision_comm::stop_track::Response& res)
{
    this->started = false;
    this->lost_it = false;
    this->storage->write_obj_transform_by_index(index, transform);
    index = -1;
    this->storage->write_tracked_index(index);
    return true;
}


bool
Tracker::cb_grasp(pacman_vision_comm::grasp_verification::Request& req,
                        pacman_vision_comm::grasp_verification::Response& res)
{
    //TODO
    return true;
}

void
Tracker::spin_once()
{
    //process this module callbacks
    this->queue_ptr->callAvailable(ros::WallDuration(0));
}

