#ifndef _INCL_STORAGE
#define _INCL_STORAGE

// ROS headers
#include <ros/ros.h>
#include <ros/console.h>
#include <ros/package.h>
#include <geometry_msgs/Pose.h>
#include <geometry_msgs/TransformStamped.h>
#include <tf/transform_broadcaster.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>
//PCL
#include <pcl/common/centroid.h>
#include <pcl/common/eigen.h>
#include <pcl/common/common.h>
#include <pcl/search/kdtree.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
//general utilities
#include <cmath>
#include <fstream>
#include <algorithm>
#include <Eigen/Dense>
#include <string>
#include <stdlib.h>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/filesystem.hpp>
#include <boost/date_time.hpp>

#include "pacman_vision/utility.h"

class Storage
{
  public:
    Storage();

    //Read and write scene methods
    bool read_scene (PC::Ptr &cloud);
    bool write_scene (PC::Ptr &cloud);
    bool read_scene_processed (PC::Ptr &cloud);
    bool write_scene_processed (PC::Ptr &cloud);
    bool read_scene (PXC::Ptr &cloud);
    bool read_scene_processed (PXC::Ptr &cloud);
    //Read and write estimated objects
    bool read_obj_clusters (boost::shared_ptr<std::vector<PXC> > &objs);
    bool write_obj_clusters (boost::shared_ptr<std::vector<PXC> > &objs);
    bool read_obj_transforms (boost::shared_ptr<std::vector<Eigen::Matrix4f> > &trans);
    bool write_obj_transforms (boost::shared_ptr<std::vector<Eigen::Matrix4f> > &trans);
    bool read_obj_names (boost::shared_ptr<std::vector<std::pair<std::string, std::string> > > &n);
    bool write_obj_names (boost::shared_ptr<std::vector<std::pair<std::string, std::string> > > &n);
    //Search for a specific object name and return its index
    bool search_obj_name (std::string n, int &idx);
    //Read and object transform by its index
    bool read_obj_transform_by_index (int idx, boost::shared_ptr<Eigen::Matrix4f> &trans);
    //Write and object transform to the specified index
    bool write_obj_transform_by_index (int idx, boost::shared_ptr<Eigen::Matrix4f> &trans);
    //Read and write arms/hands/table transforms
    bool read_left_arm(boost::shared_ptr<std::vector<Eigen::Matrix4f> > &arm);
    bool write_left_arm(boost::shared_ptr<std::vector<Eigen::Matrix4f> > &arm);
    bool read_right_arm(boost::shared_ptr<std::vector<Eigen::Matrix4f> > &arm);
    bool write_right_arm(boost::shared_ptr<std::vector<Eigen::Matrix4f> > &arm);
    void read_left_hand(boost::shared_ptr<Eigen::Matrix4f> &hand);
    bool write_left_hand(boost::shared_ptr<Eigen::Matrix4f> &hand);
    void read_right_hand(boost::shared_ptr<Eigen::Matrix4f> &hand);
    bool write_right_hand(boost::shared_ptr<Eigen::Matrix4f> &hand);
    void read_table(boost::shared_ptr<Eigen::Matrix4f> &t);
    bool write_table(boost::shared_ptr<Eigen::Matrix4f> &t);
  private:
    //untouched scene from kinect
    PC::Ptr scene;
    boost::mutex mtx_scene;
    //scene after processing
    PC::Ptr scene_processed;
    boost::mutex mtx_scene_processed;
    //cluster of objects found on scene
    std::vector<PXC> clusters;
    boost::mutex mtx_clusters;
    //Estimated transform from estimator
    std::vector<Eigen::Matrix4f> estimations;
    boost::mutex mtx_estimations;
    //naming and id-ing of estimated objects from estimator
    std::vector<std::pair<std::string, std::string> > names; //name,ID
    boost::mutex mtx_names;
    //Vito Left arm transforms
    std::vector<Eigen::Matrix4f> left_arm;
    boost::mutex mtx_left_arm;
    //Vito Right arm transforms
    std::vector<Eigen::Matrix4f> right_arm;
    boost::mutex mtx_right_arm;
    //Vito Left hand transform
    Eigen::Matrix4f left_hand;
    boost::mutex mtx_left_hand;
    //Vito Right hand transform
    Eigen::Matrix4f right_hand;
    boost::mutex mtx_right_hand;
    //Vito Table transformation
    Eigen::Matrix4f table;
    boost::mutex mtx_table;
};
#endif