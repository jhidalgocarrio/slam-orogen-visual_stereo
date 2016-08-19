/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"

#ifndef D2R
#define D2R M_PI/180.00 /** Convert degree to radian **/
#endif
#ifndef R2D
#define R2D 180.00/M_PI /** Convert radian to degree **/
#endif

//#define DEBUG_PRINTS 1

using namespace vsd_slam;

/** Process model when accumulating delta poses **/
WMTKState processModel (const WMTKState &state,  const Eigen::Vector3d &linear_velocity, const Eigen::Vector3d &angular_velocity, const double delta_t)
{
    WMTKState s2; /** Propagated state */

    /** Update rotation rate **/
    s2.angvelo = angular_velocity;

    /** Apply Rotation **/
    ::vsd_slam::vec3 scaled_axis = state.angvelo * delta_t;
    s2.orient = state.orient * ::vsd_slam::SO3::exp(scaled_axis);

    /** Update the velocity (position rate) **/
    s2.velo = state.orient * linear_velocity;

    /** Apply Translation **/
    s2.pos = state.pos + (state.velo * delta_t);

    return s2;
};


Task::Task(std::string const& name)
    : TaskBase(name)
{
    /** Set pose and landmark symbol identifiers **/
    this->pose_key = 'x';
    this->landmark_key = 'l';
    this->pose_idx = 0;

    /******************************/
    /*** Control Flow Variables ***/
    /******************************/
    this->init_flag = false;

    /**************************/
    /** Input port variables **/
    /**************************/
    this->delta_pose.invalidate();
}

Task::Task(std::string const& name, RTT::ExecutionEngine* engine)
    : TaskBase(name, engine)
{
    /** Set pose and landmark symbol identifiers **/
    this->pose_key = 'x';
    this->landmark_key = 'l';
    this->pose_idx = 0;

    /******************************/
    /*** Control Flow Variables ***/
    /******************************/
    this->init_flag = false;

    /**************************/
    /** Input port variables **/
    /**************************/
    this->delta_pose.invalidate();
}

Task::~Task()
{
}

void Task::delta_pose_samplesTransformerCallback(const base::Time &ts, const ::base::samples::RigidBodyState &delta_pose_samples_sample)
{
    #ifdef DEBUG_PRINTS
    RTT::log(RTT::Warning)<<"[VSD_SLAM DELTA_POSE ] DELTA_POSE TIME: "<<this->delta_pose.time.toMicroseconds()<<RTT::endlog();
    #endif

    if (!this->init_flag)
    {
        /** Get the transformation Tbody_sensor **/
        Eigen::Affine3d body_sensor_tf; /** Transformer transformation **/
        if (_sensor_frame.value().compare(_body_frame.value()) == 0)
        {
            body_sensor_tf.setIdentity();
        }
        else if (!_sensor2body.get(ts, body_sensor_tf, false))
        {
            RTT::log(RTT::Fatal)<<"[VSD_SLAM FATAL ERROR]  No transformation provided."<<RTT::endlog();
           return;
        }

        /** Get the transformation Tworld_navigation **/
        Eigen::Affine3d world_nav_tf; /** Transformer transformation **/

        /** Get the transformation Tworld_navigation (navigation is body_0) **/
        if (_navigation_frame.value().compare(_world_frame.value()) == 0)
        {
            world_nav_tf.setIdentity();
        }
        else if (!_navigation2world.get(ts, world_nav_tf, false))
        {
            RTT::log(RTT::Fatal)<<"[VSD_SLAM FATAL ERROR]  No transformation provided."<<RTT::endlog();
           return;
        }

        #ifdef DEBUG_PRINTS
        RTT::log(RTT::Warning)<<"[VSD_SLAM POSE_SAMPLES] - Initializing Visual Stereo Back-End..."<<RTT::endlog();
        #endif

        /** Set initial pose out with respect to the navigation frame (navigation is body_0) **/
        Eigen::Affine3d nav_sensor_tf = world_nav_tf.inverse() * body_sensor_tf; //!Initial transformation

        /** Change the initial pose out in sensor frame **/
        this->vsd_slam_pose_out.position = nav_sensor_tf.translation();
        this->vsd_slam_pose_out.orientation = Eigen::Quaternion<double>(nav_sensor_tf.rotation());
        this->vsd_slam_pose_out.velocity.setZero(); //!Initial velocity
        this->vsd_slam_pose_out.angular_velocity.setZero(); //!Initial angular velocity

        /***************************
        * BACK-END INITIALIZATION  *
        ***************************/
        Eigen::Affine3d vsd_slam_pose_out_tf = this->vsd_slam_pose_out.getTransform();
        this->initialization(vsd_slam_pose_out_tf);

        /** Increase pose index **/
        this->pose_idx++;

        /** Initialization succeeded **/
        this->init_flag = true;

        /** Set the initial Tbody_sensor **/
        this->body_sensor_tf = body_sensor_tf;

        /** Set the initial delta_pose **/
        this->delta_pose = delta_pose_samples_sample;

        /** Output port the first initial pose **/
        _pose_samples_out.write(this->vsd_slam_pose_out);

        #ifdef DEBUG_PRINTS
        RTT::log(RTT::Warning)<<"[DONE]\n";
        #endif
    }

    /** Delta time between samples **/
    const double predict_delta_t = delta_pose_samples_sample.time.toSeconds() - this->delta_pose.time.toSeconds();
    RTT::log(RTT::Warning)<<"[VSD_SLAM FATAL ERROR] predict_delta_time: "<<predict_delta_t<<RTT::endlog();

    Eigen::Affine3d body_sensor_tf; /** Transformer transformation **/

    /** Get the transformation Tbody_sensor **/
    if (_sensor_frame.value().compare(_body_frame.value()) == 0)
    {
        body_sensor_tf.setIdentity();
    }
    else if (!_sensor2body.get(ts, body_sensor_tf, false))
    {
        RTT::log(RTT::Fatal)<<"[VSD_SLAM FATAL ERROR] No transformation provided."<<RTT::endlog();
       return;
    }

    /** Calculate the delta pose Tb(k-1)_b(k) **/
    Eigen::Affine3d body_delta_pose_tf (delta_pose_samples_sample.orientation);
    body_delta_pose_tf.translation() = delta_pose_samples_sample.position;

    /** Calculate the delta pose in sensor(s) Ts(k-1)_s(k) **/
    /** Ts(k-1)_s(k) = Ts(k-1)_b(k-1) * Tb(k-1)_b(k) * Tb(k)_s(k) **/
    Eigen::Affine3d sensor_delta_pose_tf = this->body_sensor_tf.inverse() * body_delta_pose_tf * body_sensor_tf;

    /** Store the current delta_pose in sensor frame. Ts(k-1)_s(k) **/
    this->delta_pose.time = delta_pose_samples_sample.time;
    this->delta_pose.position = sensor_delta_pose_tf.translation();
    this->delta_pose.orientation =  Eigen::Quaternion <double>(sensor_delta_pose_tf.rotation());
    this->delta_pose.cov_position.setZero(); this->delta_pose.cov_orientation.setZero();
    this->delta_pose.cov_position.diagonal() = body_sensor_tf.rotation().inverse() * delta_pose_samples_sample.cov_position.diagonal();
    this->delta_pose.cov_orientation.diagonal() = body_sensor_tf.rotation().inverse() * delta_pose_samples_sample.cov_orientation.diagonal();

    /** Delta pose velocity in sensor frame vs(k-1) = Ts(k-1)_b(k-1) * vb(k-1) **/
    this->delta_pose.velocity = this->body_sensor_tf.inverse().rotation() * delta_pose_samples_sample.velocity;
    this->delta_pose.angular_velocity =  this->body_sensor_tf.inverse().rotation() * delta_pose_samples_sample.angular_velocity;
    this->delta_pose.cov_velocity = this->body_sensor_tf.inverse().rotation() * delta_pose_samples_sample.cov_velocity * this->body_sensor_tf.inverse().rotation().transpose();
    this->delta_pose.cov_angular_velocity = this->body_sensor_tf.inverse().rotation() * delta_pose_samples_sample.cov_angular_velocity * this->body_sensor_tf.inverse().rotation().transpose();

    /** Update the Tbody_sensor with the current one Tb(k)_s(k) **/
    this->body_sensor_tf = body_sensor_tf;

    /******************************************
    * Delta pose integration in sensor frame *
    * ****************************************/

    /** Process Model Uncertainty **/
    UKF::cov cov_process; cov_process.setZero();
    MTK::subblock (cov_process, &WMTKState::velo, &WMTKState::velo) = this->delta_pose.cov_velocity;
    MTK::subblock (cov_process, &WMTKState::angvelo, &WMTKState::angvelo) = this->delta_pose.cov_angular_velocity;

    /** Predict the filter state **/
    this->filter->predict(boost::bind(processModel, _1 ,
                            static_cast<const Eigen::Vector3d>(this->delta_pose.velocity),
                            static_cast<const Eigen::Vector3d>(this->delta_pose.angular_velocity),
                            predict_delta_t),
                            cov_process);

    /******************************************
    * Output port the odometry pose
    ******************************************/
    this->odo_poseOutputPort(this->delta_pose.time);
}

void Task::visual_features_samplesTransformerCallback(const base::Time &ts, const ::visual_stereo::ExteroFeatures &visual_features_samples_sample)
{

    /*****************************************/
    /** Check whether the UKF filter is set **/
    /*****************************************/
    if(!init_flag)
    {
        RTT::log(RTT::Warning)<<"[VSD_SLAM FEATURES ] FILTER STILL NOT INITIALIZED"<<RTT::endlog();
        return;
    }

    /****************************************************/
    /** Reset UKF and store the accumulated delta pose **/
    /****************************************************/
    ::base::Pose cumulative_delta_pose;
    ::base::Matrix6d cov_cumulative_delta_pose;
    this->resetUKF(cumulative_delta_pose, cov_cumulative_delta_pose);
    #ifdef DEBUG_PRINTS
    RTT::log(RTT::Warning)<<"[VSD_SLAM FEATURES ] RESET UKF\n";
    RTT::log(RTT::Warning)<<"[VSD_SLAM FEATURES ] CUMULATIVE DELTA POSE"<<RTT::endlog();
    RTT::log(RTT::Warning)<<"[VSD_SLAM FEATURES] CUMULATIVE DELTA POSITION:\n"<<cumulative_delta_pose.position<<"\n";
    RTT::log(RTT::Warning)<<"[VSD_SLAM FEATURES] CUMULATIVE DELTA ORIENTATION ROLL: "<< base::getRoll(cumulative_delta_pose.orientation)*R2D
        <<" PITCH: "<< base::getPitch(cumulative_delta_pose.orientation)*R2D<<" YAW: "<< base::getYaw(cumulative_delta_pose.orientation)*R2D<<std::endl;
    RTT::log(RTT::Warning)<<"[VSD_SLAM FEATURES] CUMULATIVE DELTA COVARIANCE:\n"<<cov_cumulative_delta_pose<<"\n";
    #endif


    /****************************************************
    **   Store the delta pose in the factor graph     **
    ****************************************************/

    /** Symbols **/
    gtsam::Symbol symbol_prev = gtsam::Symbol(this->pose_key, this->pose_idx-1);
    gtsam::Symbol symbol_current = gtsam::Symbol(this->pose_key, this->pose_idx);

    /** Add the delta pose to the factor graph. TO-DO: probably not needed **/
    /**  BetweenFactor in GTSAM **/
    this->factor_graph->add(gtsam::BetweenFactor<gtsam::Pose3>(symbol_prev, symbol_current,
                gtsam::Pose3(gtsam::Rot3(cumulative_delta_pose.orientation), gtsam::Point3(cumulative_delta_pose.position)),
                gtsam::noiseModel::Gaussian::Covariance(cov_cumulative_delta_pose)));


    /***********************************************
     * Add the cumulative delta pose to the pose
    ***********************************************/

    /** Compute the pose estimate **/
    ::base::TransformWithCovariance cumulative_delta_pose_with_cov(cumulative_delta_pose.position, cumulative_delta_pose.orientation, cov_cumulative_delta_pose);
    this->pose_with_cov =  this->pose_with_cov * cumulative_delta_pose_with_cov;

    /** Update the pose in pose state **/
    this->pose_state.pos = this->pose_with_cov.translation;
    this->pose_state.orient = static_cast<Eigen::Quaterniond>(this->pose_with_cov.orientation);

    /***********************************************
    * Store Pose in GTSAM initial estimated values
    * **********************************************/
    gtsam::Pose3 current_pose(gtsam::Rot3(this->pose_state.orient), gtsam::Point3(this->pose_state.pos));
    this->sam_values->insert(symbol_current, current_pose);

    /** Read Stereo measurement from the input port **/
    gtsam::Symbol visual_features_symbol = gtsam::Symbol(this->landmark_key, visual_features_samples_sample.img_idx);
    RTT::log(RTT::Warning)<<"[VSD_SLAM FEATURES ] LANDMARK ID: "<<std::string(visual_features_symbol)<<RTT::endlog();


    /******************************************************
    * Set Generic Stereo Factor and features pose in GTSAM
    ******************************************************/

    /********************************
    ** Output port the slam pose **
    ********************************/
    this->vsd_slam_poseOutputPort(visual_features_samples_sample.time, symbol_current);
    #ifdef DEBUG_PRINTS
    RTT::log(RTT::Warning)<<"********************************************\n";
    RTT::log(RTT::Warning)<<"[VSD_SLAM FEATURES] CURRENT POSITION:\n"<<this->pose_with_cov.translation<<"\n";
    RTT::log(RTT::Warning)<<"[VSD_SLAM FEATURESM] CURRENT ORIENTATION ROLL: "<< base::getRoll(this->pose_with_cov.orientation)*R2D
        <<" PITCH: "<< base::getPitch(this->pose_with_cov.orientation)*R2D<<" YAW: "<< base::getYaw(this->pose_with_cov.orientation)*R2D<<std::endl;
    RTT::log(RTT::Warning)<<"[VSD_SLAM FEATURES] CURRENT COVARIANCE:\n"<<this->pose_with_cov.cov<<"\n";
    #endif

    /****************************************
    ** Increase in one unit the pose index **
    ****************************************/
    this->pose_idx++;

}

/// The following lines are template definitions for the various state machine
// hooks defined by Orocos::RTT. See Task.hpp for more detailed
// documentation about them.

bool Task::configureHook()
{
    if (! TaskBase::configureHook())
        return false;

    /** Read the camera calibration parameters **/
    this->camera_calib = _calib_parameters.value();
    this->stereo_calib.reset(new gtsam::Cal3_S2Stereo(this->camera_calib.camLeft.fx,
                                                this->camera_calib.camLeft.fy,
                                                0.00,
                                                this->camera_calib.camLeft.cx,
                                                this->camera_calib.camLeft.cy,
                                                this->camera_calib.extrinsic.tx));

    /** Optimized Output port **/
    this->vsd_slam_pose_out.invalidate();
    this->vsd_slam_pose_out.sourceFrame = _vsd_slam_localization_source_frame.value();

    /** Relative Frame to port out the SAM pose samples **/
    this->vsd_slam_pose_out.targetFrame = _world_frame.value();

    /** Odometry Output port **/
    this->odo_pose_out.invalidate();
    this->odo_pose_out.sourceFrame = _odometry_localization_source_frame.value();

    /** Relative Frame to port out the Odometry pose samples **/
    this->odo_pose_out.targetFrame = this->vsd_slam_pose_out.sourceFrame;

    /***********************/
    /** Info and Warnings **/
    /***********************/
    RTT::log(RTT::Warning)<<"[VSD_SLAM TASK] DESIRED TARGET FRAME IS: "<<vsd_slam_pose_out.targetFrame<<RTT::endlog();
    RTT::log(RTT::Warning)<<"[VSD_SLAM TASK] STEREO CAMERA CALIBRATION PARAMETERS"<<RTT::endlog();
    RTT::log(RTT::Warning)<<"[VSD_SLAM TASK] FX "<<this->camera_calib.camLeft.fx<<" FY "<< this->camera_calib.camLeft.fy <<RTT::endlog();
    RTT::log(RTT::Warning)<<"[VSD_SLAM TASK] CX "<<this->camera_calib.camLeft.cx<<" CY "<< this->camera_calib.camLeft.cy <<RTT::endlog();
    RTT::log(RTT::Warning)<<"[VSD_SLAM TASK] BASELINE "<< this->camera_calib.extrinsic.tx <<"\n"<<RTT::endlog();

    return true;
}

bool Task::startHook()
{
    if (! TaskBase::startHook())
        return false;
    return true;
}
void Task::updateHook()
{
    TaskBase::updateHook();
}
void Task::errorHook()
{
    TaskBase::errorHook();
}
void Task::stopHook()
{
    TaskBase::stopHook();
}
void Task::cleanupHook()
{
    TaskBase::cleanupHook();

    /** Reset prediction filter **/
    this->filter.reset();

    /** Reset GTSAM **/
    this->factor_graph.reset();
    this->sam_values.reset();

    /** Reset estimation **/
    this->pose_idx = 0;
}

void Task::initialization(Eigen::Affine3d &tf)
{
    /** The filter vector state variables for the navigation quantities **/
    WMTKState statek_0;

    /******************************/
    /** Initialize the Back-End  **/
    /******************************/

    /** Initial covariance matrix **/
    UKF::cov cov_statek_0; /** Initial P(0) for the state **/
    cov_statek_0.setZero();
    MTK::setDiagonal (cov_statek_0, &WMTKState::pos, 1e-10);
    MTK::setDiagonal (cov_statek_0, &WMTKState::orient, 1e-10);
    MTK::setDiagonal (cov_statek_0, &WMTKState::velo, 1e-12);
    MTK::setDiagonal (cov_statek_0, &WMTKState::angvelo, 1e-12);

    /***************************/
    /**  MTK initialization   **/
    /***************************/
    this->initUKF(statek_0, cov_statek_0);

    /***************************/
    /**  SAM initialization   **/
    /***************************/
    gtsam::Symbol frame_id = gtsam::Symbol(this->pose_key, this->pose_idx);
    gtsam::Pose3 first_pose(gtsam::Rot3(this->vsd_slam_pose_out.orientation), gtsam::Point3(this->vsd_slam_pose_out.position));

    /** Create the factor graph **/
    this->factor_graph.reset(new gtsam::NonlinearFactorGraph());

    /** Constrain the first pose such that it cannot change from its original value during optimization
    NOTE: NonlinearEquality forces the optimizer to use QR rather than Cholesky
    QR is much slower than Cholesky, but numerically more stable **/
    this->factor_graph->push_back(gtsam::NonlinearEquality<gtsam::Pose3>(frame_id, first_pose));

    /** Create the estimated values **/
    this->sam_values.reset(new gtsam::Values());

    /** Insert first pose in initial estimates **/
    this->sam_values->insert(frame_id, first_pose);

    /***************************************/
    /** Accumulate pose in MTK state form **/
    /***************************************/
    this->pose_state.pos = tf.translation(); //!Initial position
    this->pose_state.orient = Eigen::Quaternion<double>(tf.rotation());

    /** Set the initial velocities in the state vector **/
    this->pose_state.velo.setZero(); //!Initial linear velocity
    this->pose_state.angvelo.setZero(); //!Initial angular velocity

    /** Accumulate pose in TWC form **/
    this->pose_with_cov.translation = tf.translation();
    this->pose_with_cov.orientation = this->pose_state.orient;

    return;
}

void Task::initUKF(WMTKState &statek, UKF::cov &statek_cov)
{
    /** Create the filter **/
    this->filter.reset (new UKF (statek, statek_cov));

    #ifdef DEBUG_PRINTS
    WMTKState state = this->filter->mu();
    RTT::log(RTT::Warning)<< RTT::endlog();
    RTT::log(RTT::Warning)<<"[VSD_SLAM INIT UKF] State P0|0 is of size " <<this->filter->sigma().rows()<<" x "<<filter->sigma().cols()<< RTT::endlog();
    RTT::log(RTT::Warning)<<"[VSD_SLAM INIT UKF] State P0|0:\n"<<this->filter->sigma()<< RTT::endlog();
    RTT::log(RTT::Warning)<<"[VSD_SLAM INIT UKF] state:\n"<<state<< RTT::endlog();
    RTT::log(RTT::Warning)<<"[VSD_SLAM INIT UKF] position:\n"<<state.pos<< RTT::endlog();
    RTT::log(RTT::Warning)<<"[VSD_SLAM INIT UKF] orientation Roll: "<< base::getRoll(state.orient)*R2D
        <<" Pitch: "<< base::getPitch(state.orient)*R2D<<" Yaw: "<< base::getYaw(state.orient)*R2D<< RTT::endlog();
    RTT::log(RTT::Warning)<<"[VSD_SLAM INIT UKF] velocity:\n"<<state.velo<<"\n";
    RTT::log(RTT::Warning)<<"[VSD_SLAM INIT UKF] angular velocity:\n"<<state.angvelo<<"\n";
    RTT::log(RTT::Warning)<< RTT::endlog();
    #endif


}

void Task::resetUKF(::base::Pose &current_delta_pose, ::base::Matrix6d &cov_current_delta_pose)
{

    /** Compute the delta pose since last time we reset the filter **/
    current_delta_pose.position = this->filter->mu().pos;// Delta position
    current_delta_pose.orientation = this->filter->mu().orient;// Delta orientation

    /** Compute the delta covariance since last time we reset the filter **/
    cov_current_delta_pose = this->filter->sigma().block<6,6>(0,0);

    /** Update the velocity in the pose state **/
    this->pose_state.velo = this->pose_state.orient * this->filter->mu().velo;// current linear velocity
    this->pose_state.angvelo = this->pose_state.orient * this->filter->mu().angvelo;// current angular velocity

    /** Reset covariance matrix **/
    UKF::cov P(UKF::cov::Zero());
    MTK::setDiagonal (P, &WMTKState::pos, 1e-10);
    MTK::setDiagonal (P, &WMTKState::orient, 1e-10);
    MTK::setDiagonal (P, &WMTKState::velo, 1e-12);
    MTK::setDiagonal (P, &WMTKState::angvelo, 1e-12);

    /** Remove the filter **/
    this->filter.reset();

    /** Create and reset a new filter **/
    WMTKState statek;
    this->initUKF(statek, P);

    return;
}

void Task::odo_poseOutputPort(const base::Time &timestamp)
{
    WMTKState statek = this->filter->mu();

    /** Out port the last odometry pose **/
    this->odo_pose_out.position = statek.pos;
    this->odo_pose_out.cov_position = this->filter->sigma().block<3,3>(0,0);
    this->odo_pose_out.orientation = statek.orient;
    this->odo_pose_out.cov_orientation = this->filter->sigma().block<3,3>(3,3);
    this->odo_pose_out.velocity = statek.velo;
    this->odo_pose_out.cov_velocity =  this->filter->sigma().block<3,3>(6,6);
    this->odo_pose_out.angular_velocity = statek.angvelo;
    this->odo_pose_out.cov_angular_velocity =  this->filter->sigma().block<3,3>(9,9);
    _odo_pose_samples_out.write(this->odo_pose_out);

}

void Task::vsd_slam_poseOutputPort(const base::Time &timestamp, const gtsam::Symbol &symbol)
{
//    const gtsam::Values pose_values = this->sam_values->filter<gtsam::Pose3>();
//    const gtsam::Pose3 last_pose = reinterpret_cast<const gtsam::Pose3&>(pose_values.at(symbol));

    /** Out port the last slam pose **/
//    this->vsd_slam_pose_out.position = last_pose.translation().vector();
//    this->odo_pose_out.cov_position = this->filter->sigma().block<3,3>(0,0);
//    this->vsd_slam_pose_out.orientation = last_pose.rotation().toQuaternion();
//    this->odo_pose_out.cov_orientation = this->filter->sigma().block<3,3>(3,3);
//    this->odo_pose_out.velocity = statek.velo;
//    this->odo_pose_out.cov_velocity =  this->filter->sigma().block<3,3>(6,6);
//    this->odo_pose_out.angular_velocity = statek.angvelo;
//    this->odo_pose_out.cov_angular_velocity =  this->filter->sigma().block<3,3>(9,9);
    this->vsd_slam_pose_out.position = this->pose_state.pos;
    this->vsd_slam_pose_out.orientation = this->pose_state.orient;
    _pose_samples_out.write(this->vsd_slam_pose_out);

}
