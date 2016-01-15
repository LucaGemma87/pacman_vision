#include <tracker_gui.h>
#include <ui_tracker_gui.h>

TrackerGui::TrackerGui(const pacv::TrackerConfig::Ptr conf, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::TrackerGui), running(false)
{
    config = conf;
    ui->setupUi(this);
    init();
}
TrackerGui::~TrackerGui()
{
    delete ui;
}

QListWidget*
TrackerGui::getObjList() const
{
    return ui->objects;
}

QPushButton*
TrackerGui::getRunButt() const
{
    return ui->RunningButt;
}
QPushButton*
TrackerGui::getTrackButt() const
{
    return ui->TrackButt;
}
QPushButton*
TrackerGui::getStopButt() const
{
    return ui->StopButt;
}

QPushButton*
TrackerGui::getRefreshButt() const
{
    return ui->RefreshO;
}
QWidget*
TrackerGui::getWidget() const
{
    return ui->TrackerW;
}

void
TrackerGui::setRunning(const bool run)
{
    running=run;
}

void TrackerGui::init()
{
    bool value;
    config->get("publish_bounding_box", value);
    ui->PubButt->setChecked(value);
    ui->status->setStyleSheet("QLabel {color : red}");
    ui->StopButt->setDisabled(true);
    ui->TrackButt->setDisabled(true);
    ui->objects->clear();
}

void TrackerGui::on_RunningButt_clicked()
{
    if (running){
        //kill him
        ui->RunningButt->setText("Spawn me");
        ui->status->setText("Not Running");
        ui->status->setStyleSheet("QLabel {color : red}");
        ui->TrackerF->setDisabled(true);
        return;
    }
    else{
        //spawn him
        ui->RunningButt->setText("Kill me");
        ui->status->setText("Running");
        ui->status->setStyleSheet("QLabel {color : green}");
        ui->TrackerF->setDisabled(false);
        return;
    }
}

void TrackerGui::on_objects_itemSelectionChanged()
{
    ui->TrackButt->setDisabled(false);
}

void TrackerGui::on_TrackButt_clicked()
{
    ui->TrackButt->setDisabled(true);
    ui->StopButt->setDisabled(false);
    QListWidgetItem *item = ui->objects->currentItem();
    std::string obj = item->text().toStdString();
    emit trackObject(&obj);
}

void TrackerGui::on_StopButt_clicked()
{
   ui->StopButt->setDisabled(true);
}
