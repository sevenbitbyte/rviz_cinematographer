#include "multi_object_tracker.h"


namespace MultiObjectTracker {

  Tracker::Tracker():
    m_debug_counter(0)
    {
    ros::NodeHandle n("~");
    ros::NodeHandle pub_n;

    m_algorithm = new MultiObjectTrackerAlgorithm();
    m_transformListener = new tf::TransformListener();


    m_box_detection_1_subscriber = pub_n.subscribe< box_detection ::BoxDetections >("box_detections", 30, &Tracker  ::boxDetectionCallback, this);
    m_box_detection_2_subscriber = n.subscribe< box_detection ::BoxDetections >("/box_detections2", 30, &Tracker ::boxDetectionCallback, this);
    m_box_detection_3_subscriber = n.subscribe< box_detection ::BoxDetections >("/box_detections3", 30, &Tracker ::boxDetectionCallback, this);
    m_laser_detection_subscriber = n.subscribe< geometry_msgs ::PoseArray >("/object_poses", 30, &Tracker ::laserDetectionCallback, this);
    m_picked_objectsSubcriber    = pub_n.subscribe< box_detection ::BoxDetections >("object_picked", 30, &Tracker   ::object_picked_callback, this);

    m_hypothesis_box_msg_publisher = pub_n.advertise< box_detection     ::BoxDetections >( "object_tracks_box_msg_tracked" , 1 );
    m_hypothesis_future_box_msg_publisher = pub_n.advertise< box_detection     ::BoxDetections >( "object_tracks_box_msg" , 1 );
    m_measurementMarkerPublisher   = n.advertise< visualization_msgs::Marker >( n.getNamespace()+ "/measurements", 1 );
    m_hypothesesPublisher          = n.advertise< visualization_msgs::Marker >( n.getNamespace()+ "/object_tracks" , 1 );
    m_track_linePublisher          = n.advertise< visualization_msgs::Marker >( n.getNamespace()+ "/object_tracks_line" , 1 );
    m_measurementCovPublisher      = n.advertise< visualization_msgs::Marker >( n.getNamespace()+ "/measurements_covariances" , 1 );
    m_hypothesisCovPublisher       = n.advertise< visualization_msgs::Marker >( n.getNamespace()+ "/hypotheses_covariances" , 1 );
    m_static_objectsPublisher      = n.advertise< visualization_msgs::Marker >( n.getNamespace()+ "/static_objects" , 1 );
    m_dynamic_objectsPublisher     = n.advertise< visualization_msgs::Marker >( n.getNamespace()+ "/dynamic_objects" , 1 );
    m_picked_objectsPublisher      = n.advertise< visualization_msgs::Marker >( n.getNamespace()+ "/picked_objects" , 1 );
    m_debug_publisher              = n.advertise< multi_object_tracking::DebugTracking >( n.getNamespace()+ "/debug" , 1 );
    m_hypotheses_future_publisher  = n.advertise< visualization_msgs::Marker >( n.getNamespace()+ "/future_tracks" , 1 );

    m_tracking_reset = n.advertiseService("tracking_reset", &Tracker::tracking_reset, this);


    m_radius_around_picked            = getParamElseDefault<double> ( n, "m_radius_around_picked", 0.1 );
    m_merge_close_hypotheses_distance = getParamElseDefault<double> ( n, "m_merge_close_hypotheses_distance", 0.1 );
    m_max_mahalanobis_distance        = getParamElseDefault<double> ( n, "m_max_mahalanobis_distance", 3.75 );
    m_world_frame                     = getParamElseDefault<std::string> ( n, "m_world_frame", "world" );
    m_born_time_threshold             = getParamElseDefault<double> ( n, "m_born_time_threshold", 0.5 );
    m_future_time                     = getParamElseDefault<double> ( n, "m_future_time", 0.0 );
    // m_spurious_time                   = getParamElseDefault<double> ( n, "m_spurious_time", 4.0 );
    // m_time_start_velocity_decay       = getParamElseDefault<double> ( n, "m_time_start_velocity_decay", 1.0 );
    // m_time_finish_velocity_decay      = getParamElseDefault<double> ( n, "m_time_finish_velocity_decay", 4.0 );


    m_algorithm->set_merge_close_hypotheses_distance (m_merge_close_hypotheses_distance);
    m_algorithm->m_multiHypothesisTracker.set_max_mahalanobis_distance(m_max_mahalanobis_distance);
    // m_algorithm->m_multiHypothesisTracker.set_spurious_time(m_spurious_time);
    // m_algorithm->m_multiHypothesisTracker.set_time_start_velocity_decay (m_time_start_velocity_decay);
    // m_algorithm->m_multiHypothesisTracker.set_time_finish_velocity_decay (m_time_finish_velocity_decay);



    //HACK only for making fake pickings
    program_start_time= MultiHypothesisTracker::get_time_high_res();
    did_fake_pick=false;


  }

  Tracker::~Tracker() {
  }

  void Tracker::update() {
    // std::cout << std::endl << "predict without measurement" << '\n';
    m_algorithm->predictWithoutMeasurement();
  }

  void Tracker::boxDetectionCallback(const box_detection::BoxDetections::ConstPtr& msg){

    // std::cout << std::endl << "received box detections: " <<  msg->box_detections.size() << '\n' ;

    std::vector <Measurement> measurements = box_detections2measurements(msg);

    // double curr_time=MultiHypothesisTracker::get_time_high_res();
    // if (  int(curr_time-program_start_time) % 20 ==0 ){
    //   std::cout << "DID PICK------------------------------------------------------------------" << '\n';
    //   for (size_t i = 0; i < msg->box_detections.size(); i++) {
    //     Measurement measurement;
    //     measurement.pos=vnl_vector<double>(3);
    //     measurement.pos(0)=msg->box_detections[i].position.x;
    //     measurement.pos(1)=msg->box_detections[i].position.y;
    //     measurement.pos(2)=msg->box_detections[i].position.z;
    //     std::cout << "postion of pick: "  << measurement.pos  << '\n';
    //   }
    //   std::cout << "-------------------------------------------------------------------------" << '\n';
    //   object_picked_callback(msg);
    //   did_fake_pick=true;
    // }



    generic_detection_handler(measurements);

  }

  void Tracker::laserDetectionCallback(const geometry_msgs::PoseArray::ConstPtr& msg){
    // ROS_INFO_STREAM("laser callback");

    std::vector <Measurement> measurements = laser_detections2measurements(msg);

    generic_detection_handler(measurements);

  }



  std::vector <Measurement> Tracker::box_detections2measurements (const box_detection::BoxDetections::ConstPtr& msg){
    Measurement measurement;
    std::vector <Measurement> measurements;


    for (size_t i = 0; i < msg->box_detections.size(); i++) {
      measurement.pos=vnl_vector<double>(3);
      measurement.pos(0)=msg->box_detections[i].position.x;
      measurement.pos(1)=msg->box_detections[i].position.y;
      measurement.pos(2)=msg->box_detections[i].position.z;

      //TODO set covariance for the measurement to be dyamic depending on the altitude of the drone
      // vnl_matrix< double > measurementCovariance = vnl_matrix< double >( 3, 3 );
  		// measurementCovariance.set_identity();
  		// double measurementStd = 0.04;
  		// measurementCovariance( 0, 0 ) = measurementStd * measurementStd;
  		// measurementCovariance( 1, 1 ) = measurementStd * measurementStd;
  		// measurementCovariance( 2, 2 ) = measurementStd * measurementStd;
      // measurement.cov=measurementCovariance;


      //Setting covariance from the message
      vnl_matrix< double > measurementCovariance = vnl_matrix< double >( 3, 3 );
  		measurementCovariance.set_identity();
  		double measurementStd = msg->box_detections[i].position_covariance_xy[0] * 1;   //Multiply it by a constant so that it is in the same range we chose earlier
  		measurementCovariance( 0, 0 ) = measurementStd;
  		measurementCovariance( 1, 1 ) = measurementStd;
  		measurementCovariance( 2, 2 ) = measurementStd;
      measurement.cov=measurementCovariance;



      // std::cout << "measurmnt covariance before increase with distance is" << measurement.cov<< '\n';


      measurement.color=msg->box_detections[i].color;
      // measurement.color='Y';
      measurement.frame=msg->header.frame_id;
      measurement.time=msg->header.stamp.toSec();


      //get rotation of drone from tf tree. At the time the measurement was took
      double drone_angle;
      try{
        tf::StampedTransform transform_footprint_to_base_link;
        m_transformListener->lookupTransform ("base_link", "base_footprint", msg->header.stamp, transform_footprint_to_base_link);
        tf::Quaternion q=transform_footprint_to_base_link.getRotation();;
        measurement.rotation_angle=q.getAngle();
      }catch(tf::TransformException& ex) {
        ROS_ERROR("Received an exception trying get the rotaton of the drone");
        measurement.rotation_angle=0.0;
      }
      drone_angle=measurement.rotation_angle;


      //If the angle is big, increase the uncertanty
      // double cov_increase=exp(drone_angle);  //Angle of 0 will leave the uncertanty as it is. Angles bigger will increase the covariance;
      // measurementCovariance( 0, 0 ) *= cov_increase;
      // measurementCovariance( 1, 1 ) *= cov_increase;
      // measurementCovariance( 2, 2 ) *= cov_increase;
      measurement.cov=measurementCovariance;


      // std::cout << "measurmnt covariance after increase with distance is" << measurement.cov<< '\n';



      measurements.push_back(measurement);
    }

    return measurements;

  }


  std::vector <Measurement> Tracker::laser_detections2measurements(const geometry_msgs::PoseArray::ConstPtr& msg){
    Measurement measurement;
    std::vector <Measurement> measurements;


    for (size_t i = 0; i < msg->poses.size(); i++) {
      measurement.pos=vnl_vector<double>(3);
      measurement.pos(0)=msg->poses[i].position.x;
      measurement.pos(1)=msg->poses[i].position.y;
      measurement.pos(2)=msg->poses[i].position.z;

      //TODO set covariance for the measurement to be dyamic depending on the altitude of the drone
      vnl_matrix< double > measurementCovariance = vnl_matrix< double >( 3, 3 );
      measurementCovariance.set_identity();
      double measurementStd = 0.03;
      measurementCovariance( 0, 0 ) = measurementStd * measurementStd;
      measurementCovariance( 1, 1 ) = measurementStd * measurementStd;
      measurementCovariance( 2, 2 ) = measurementStd * measurementStd;
      measurement.cov=measurementCovariance;

      measurement.color='U';

      measurement.frame=msg->header.frame_id;



      measurement.time=msg->header.stamp.toSec();

      measurements.push_back(measurement);
    }

    return measurements;

  }


  void Tracker::object_picked_callback(const box_detection::BoxDetections::ConstPtr& msg){
    //get the position that was picked
    //get the color that was picked
    ROS_DEBUG("Tracking received that an object was picked");

    std::vector <Measurement> picked_measurements = box_detections2measurements(msg);
    bool ret=transform_to_frame(picked_measurements, m_world_frame);
    if (ret==false){
      std::cout << "couldn't invalidate the hypothesis for a picked object because it was not transformed into the correct frame" << '\n';
      return;
    }

    for (size_t i = 0; i < picked_measurements.size(); i++) {

      // //Static oject was picked
      // if (picked_measurements[i].color=='R' || picked_measurements[i].color=='G' || picked_measurements[i].color=='B' || picked_measurements[i].color=='O'){
      //   m_picked_static_positions.push_back(picked_measurements[i]);
      // }
      //
      // //Dynamic yellow object was picked. Have to signal the hypothesis that the object on it was picked
      // if (picked_measurements[i].color=='Y'){
        block_hypothesis_near_measurement (picked_measurements[i]);
      // }

      // //sanity check
      // if (picked_measurements[i].color=='U'){
      //   ROS_DEBUG("object_picked_callback::picked object has unknown color");
      // }


    }


  }


  void Tracker::block_picked_static_measurements (std::vector<Measurement>& measurements){


    double radius=m_radius_around_picked;  //Radius around the picked position around which we will block measurements
    std::vector< Measurement >::iterator mes;


    for (size_t j = 0; j < m_picked_static_positions.size(); j++) {
      for(mes = measurements.begin(); mes != measurements.end();){

          double dist = ( (*mes).pos - m_picked_static_positions[j].pos).two_norm();
          if (dist < radius && ( (*mes).color==m_picked_static_positions[j].color  || (*mes).color=='U' ) ){
            mes = measurements.erase(mes);
          }else{
              ++mes;
          }

      }
    }


  }


  void  Tracker::block_hypothesis_near_measurement(Measurement measurement){
    double radius=m_radius_around_picked;
    std::vector<MultiHypothesisTracker::Hypothesis*> hypotheses = m_algorithm->getHypotheses();
    MultiHypothesisTracker::Hypothesis* closest_hypothesis=0;
    double closest_dist=std::numeric_limits<double>::max();

    for (size_t i = 0; i < hypotheses.size(); i++) {
      MultiObjectHypothesis *hypothesis = (MultiObjectHypothesis *) hypotheses[i];

      //grab the closest hypothesis in a certain radius that has the same color as the object we picked
      double dist = ( hypothesis->getMean() - measurement.pos).two_norm();
      // std::cout << "dist is " << dist << '\n';
      if (dist < radius && dist < closest_dist &&  ( hypothesis->getColor()== measurement.color ) ){
        closest_hypothesis=hypothesis;
        closest_dist=dist;
      }

    }

    // std::cout << "closest hypothesis is at " << closest_dist << '\n';

    if (closest_hypothesis){
        closest_hypothesis->set_picked(true);
    }

  }

  void Tracker::generic_detection_handler(std::vector<Measurement>& measurements){

    publish_debug();


    block_picked_static_measurements (measurements);


    bool ret= transform_to_frame(measurements,m_world_frame);  //transform from measurement_frame into field_frame
    if (ret==false){
      return;
    }

    publish_mesurement_markers(measurements);
    publish_mesurement_covariance(measurements);

    std::string source = "all";
    m_algorithm->objectDetectionDataReceived( measurements, source);


    // std::vector< unsigned int > assignments = m_algorithm->objectDetectionDataReceived( measurements, source);

    //Full track for first hypothesis
    // std::vector<MultiHypothesisTracker::Hypothesis*> hypotheses = m_algorithm->getHypotheses();
    // MultiObjectHypothesis *hypothesis = (MultiObjectHypothesis *) hypotheses[0];
    // const vnl_vector<double> &mean = hypothesis->getMean();
    // geometry_msgs::Point p;
    // p.x=mean(0);
    // p.y=mean(1);
    // p.z=mean(2);
    // full_track.points.push_back(p);
    // m_track_linePublisher.publish(full_track);

  }

  bool Tracker::transform_to_frame(std::vector<Measurement>&  measurements, std::string target_frame){
    for (size_t i = 0; i <  measurements.size(); i++) {
      geometry_msgs::PointStamped mes_in_origin_frame;
      geometry_msgs::PointStamped mes_in_target_frame;
      mes_in_origin_frame.header.frame_id = measurements[i].frame;
      mes_in_target_frame.header.frame_id = target_frame;

      try {
        mes_in_origin_frame.point.x = measurements[i].pos(0);
        mes_in_origin_frame.point.y = measurements[i].pos(1);
        mes_in_origin_frame.point.z = measurements[i].pos(2);
        m_transformListener->transformPoint( target_frame, mes_in_origin_frame, mes_in_target_frame );
        measurements[i].pos(0)      =mes_in_target_frame.point.x;
        measurements[i].pos(1)      =mes_in_target_frame.point.y;
        measurements[i].pos(2)      =mes_in_target_frame.point.z;
        measurements[i].frame       =target_frame;
        // std::cout << "point transformed is" << measurements[i].pos << '\n';
      }
      catch(tf::TransformException& ex) {
        ROS_ERROR("Received an exception trying to transform a point from \"%s\" to \"%s\"", mes_in_origin_frame.header.frame_id.c_str(), mes_in_target_frame.header.frame_id.c_str());
        return false;
      }
    }

    return true;

  }

  visualization_msgs::Marker Tracker::createMarker(int x, int y, int z , float r, float g, float b){
    visualization_msgs::Marker marker;
    marker.header.frame_id    = m_world_frame;
    marker.header.stamp       = ros::Time::now();
    marker.ns                 = "multi_object_tracking";
    marker.id                 = 0;
    // marker.type            = visualization_msgs::Marker::SPHERE;
    marker.type               = visualization_msgs::Marker::POINTS;
    marker.action             = visualization_msgs::Marker::ADD;
    marker.pose.position.x    = x;
    marker.pose.position.y    = y;
    marker.pose.position.z    = z;
    marker.pose.orientation.x = 0.0;
    marker.pose.orientation.y = 0.0;
    marker.pose.orientation.z = 0.0;
    marker.pose.orientation.w = 1.0;
    marker.scale.x            = 0.1;
    marker.scale.y            = 0.1;
    marker.scale.z            = 0.1;
    marker.color.a            = 1.0; // Don't forget to set the alpha!
    marker.color.r            = r;
    marker.color.g            = g;
    marker.color.b            = b;
    marker.lifetime           = ros::Duration(1.0);
    return marker;
  }

  void Tracker::publish_mesurement_markers(std::vector<Measurement>  measurements){

    if (measurements.empty()){
      return;
    }

    visualization_msgs::Marker mes_marker= createMarker(0,0,0 , 1.0, 0.0, 0.0); //red marker
    mes_marker.points.resize(measurements.size());
    mes_marker.header.frame_id = m_world_frame;

    for (size_t i = 0; i <  measurements.size(); i++) {
      mes_marker.points[i].x = measurements[i].pos(0);
      mes_marker.points[i].y = measurements[i].pos(1);
      mes_marker.points[i].z = measurements[i].pos(2);
      // std::cout << "publish_mesurement_markers:: box "<< i << " is " <<  measurements[i].pos << '\n';
    }
    m_measurementMarkerPublisher.publish(mes_marker);


  }

  void Tracker::publish_mesurement_covariance(std::vector<Measurement> measurements ){

    for (size_t i = 0; i < measurements.size(); i++) {
      visualization_msgs::Marker markerCov;
      markerCov.header.frame_id    = measurements[i].frame;
      markerCov.header.stamp       = ros::Time::now();
      markerCov.ns                 = "multi_object_tracking";
      markerCov.id                 = 1+i;
      markerCov.type               = visualization_msgs::Marker::SPHERE;
      markerCov.action             = visualization_msgs::Marker::ADD;
      markerCov.pose.position.x    = measurements[i].pos(0);
      markerCov.pose.position.y    = measurements[i].pos(1);
      markerCov.pose.position.z    = measurements[i].pos(2);
      markerCov.pose.orientation.x = 0.0;
      markerCov.pose.orientation.y = 0.0;
      markerCov.pose.orientation.z = 0.0;
      markerCov.pose.orientation.w = 1.0;

      markerCov.scale.x            = sqrt( 4.204 ) * sqrt(measurements[i].cov(0, 0));
      markerCov.scale.y            = sqrt( 4.204 ) * sqrt(measurements[i].cov(1, 1));
      markerCov.scale.z            = sqrt( 4.204 ) * sqrt(measurements[i].cov(2, 2));

      markerCov.color.r            = 1.f;
      markerCov.color.g            = 0.0f;
      markerCov.color.b            = 0.0f;
      markerCov.color.a            = 0.5f;
      markerCov.lifetime           = ros::Duration(1.0);
      m_measurementCovPublisher.publish( markerCov );
    }



  }

  void Tracker::publish_hypothesis_covariance( ){
    std::vector<MultiHypothesisTracker::Hypothesis*> hypotheses = m_algorithm->getHypotheses();
    double cur= MultiHypothesisTracker::get_time_high_res();

    for (size_t i = 0; i < hypotheses.size(); i++) {
      visualization_msgs::Marker markerCov;
      markerCov.header.frame_id    = m_world_frame;
      markerCov.header.stamp       = ros::Time::now();
      markerCov.ns                 = "multi_object_tracking";
      markerCov.id                 = 1+i;
      markerCov.type               = visualization_msgs::Marker::SPHERE;
      markerCov.action             = visualization_msgs::Marker::ADD;
      markerCov.pose.position.x    = hypotheses[i]->getMean()(0);
      markerCov.pose.position.y    = hypotheses[i]->getMean()(1);
      markerCov.pose.position.z    = hypotheses[i]->getMean()(2);
      markerCov.pose.orientation.x = 0.0;
      markerCov.pose.orientation.y = 0.0;
      markerCov.pose.orientation.z = 0.0;
      markerCov.pose.orientation.w = 1.0;


      markerCov.scale.x            = sqrt( 4.204 ) * sqrt(hypotheses[i]->getCovariance()(0, 0));
      markerCov.scale.y            = sqrt( 4.204 ) * sqrt(hypotheses[i]->getCovariance()(1, 1));
      markerCov.scale.z            = sqrt( 4.204 ) * sqrt(hypotheses[i]->getCovariance()(2, 2));

      markerCov.color.r            = 0.0f;
      markerCov.color.g            = 1.0f;
      markerCov.color.b            = 0.0f;
      markerCov.color.a            = 0.5f;
      markerCov.lifetime           = ros::Duration(1.0);



      if (!hypotheses[i]->is_picked()  &&  ( cur -  hypotheses[i]->get_born_time() > m_born_time_threshold )  ){
        m_hypothesisCovPublisher.publish( markerCov );
      }

    }


  }



  void Tracker::publishHypotheses() {

    std::vector<MultiHypothesisTracker::Hypothesis*> hypotheses = m_algorithm->getHypotheses();
    visualization_msgs::Marker hypothesis_marker= createMarker();
    double cur= MultiHypothesisTracker::get_time_high_res();

    // Publish tracks
    for(size_t i = 0; i < hypotheses.size(); ++i)
    {
      MultiObjectHypothesis *hypothesis = (MultiObjectHypothesis *) hypotheses[i];

      const vnl_vector<double> &mean = hypothesis->getMean();
      // std::cout << "publishhypothesis: mean of hypothesis " << i << " is " << mean << '\n';
      // std::cout << "color is-------------------------------------" << hypothesis->getColor() << '\n';
      geometry_msgs::Point p;
      p.x=mean(0);
      p.y=mean(1);
      p.z=mean(2);

      if (!hypothesis->is_picked()   && (cur - hypothesis->get_born_time() > m_born_time_threshold )  ){
        hypothesis_marker.points.push_back(p);
      }
      // m_hypothesesPublisher.publish (createMaker(mean(0),mean(1), mean(2), 0.0, 1.0, 0.0)); //green marker
    }
    m_hypothesesPublisher.publish (hypothesis_marker);

    // 		m_algorithm->unlockHypotheses();


  }

  void Tracker::publish_hypotheses_box_msg(){
    std::vector<MultiHypothesisTracker::Hypothesis*> hypotheses = m_algorithm->getHypotheses();
    double cur= MultiHypothesisTracker::get_time_high_res();
    box_detection::BoxDetections box_detecions;
    box_detection::BoxDetection box;
    box_detecions.header.stamp=ros::Time::now();
    box_detecions.header.frame_id=m_world_frame;


    // Publish tracks
    for(size_t i = 0; i < hypotheses.size(); ++i)
    {
      MultiObjectHypothesis *hypothesis = (MultiObjectHypothesis *) hypotheses[i];

      const vnl_vector<double> &mean = hypothesis->getMean();
      box.position.x=mean(0);
      box.position.y=mean(1);
      box.position.z=mean(2);
      box.color=hypothesis->getColor();

      if (!hypothesis->is_picked()   && (cur - hypothesis->get_born_time() > m_born_time_threshold )  ){
        box_detecions.box_detections.push_back(box);
      }
    }
    m_hypothesis_box_msg_publisher.publish (box_detecions);

  }

  void Tracker::publish_hypotheses_future_box_msg(){
    std::vector<MultiHypothesisTracker::Hypothesis*> hypotheses = m_algorithm->getHypotheses();
    double cur= MultiHypothesisTracker::get_time_high_res();
    box_detection::BoxDetections box_detecions;
    box_detection::BoxDetection box;
    box_detecions.header.stamp=ros::Time::now();
    box_detecions.header.frame_id=m_world_frame;


    // Publish tracks
    for(size_t i = 0; i < hypotheses.size(); ++i)
    {
      MultiObjectHypothesis *hypothesis = (MultiObjectHypothesis *) hypotheses[i];
      vnl_vector<double> mean = hypothesis->getMean();

      //Predict a little bit into the future
      mean += hypothesis->get_velocity()* m_future_time;

      box.position.x=mean(0);
      box.position.y=mean(1);
      box.position.z=mean(2);
      box.color=hypothesis->getColor();

      if (!hypothesis->is_picked()   && (cur - hypothesis->get_born_time() > m_born_time_threshold )  ){
        box_detecions.box_detections.push_back(box);
      }
    }
    m_hypothesis_future_box_msg_publisher.publish (box_detecions);

  }

  void Tracker::publish_hypotheses_future() {

    std::vector<MultiHypothesisTracker::Hypothesis*> hypotheses = m_algorithm->getHypotheses();
    visualization_msgs::Marker hypothesis_marker= createMarker();
    hypothesis_marker.color.r=0.0;
    hypothesis_marker.color.g=0.0;
    hypothesis_marker.color.b=1.0;
    double cur= MultiHypothesisTracker::get_time_high_res();

    // Publish tracks
    for(size_t i = 0; i < hypotheses.size(); ++i)
    {
      MultiObjectHypothesis *hypothesis = (MultiObjectHypothesis *) hypotheses[i];

      vnl_vector<double> mean = hypothesis->getMean();

      //Predict a little bit into the future
      mean += hypothesis->get_velocity()* m_future_time;


      geometry_msgs::Point p;
      p.x=mean(0);
      p.y=mean(1);
      p.z=mean(2);

      if (!hypothesis->is_picked()   && (cur - hypothesis->get_born_time() > m_born_time_threshold )  ){
        hypothesis_marker.points.push_back(p);
      }
      // m_hypothesesPublisher.publish (createMaker(mean(0),mean(1), mean(2), 0.0, 1.0, 0.0)); //green marker
    }
    m_hypotheses_future_publisher.publish (hypothesis_marker);

    // 		m_algorithm->unlockHypotheses();

  }

  void Tracker::publish_static_hypotheses(){
    std::vector<MultiHypothesisTracker::Hypothesis*> hypotheses = m_algorithm->getHypotheses();
    visualization_msgs::Marker static_object_marker= createMarker();
    static_object_marker.type = visualization_msgs::Marker::LINE_LIST;
    static_object_marker.color.a = 0.5; // Don't forget to set the alpha!
    double cur= MultiHypothesisTracker::get_time_high_res();

    // Publish tracks
    for(size_t i = 0; i < hypotheses.size(); ++i){
      MultiObjectHypothesis *hypothesis = (MultiObjectHypothesis *) hypotheses[i];

      if (hypothesis->isStatic() && !hypothesis->is_picked() && (cur - hypothesis->get_born_time() > m_born_time_threshold )    ){
        const vnl_vector<double> &mean = hypothesis->getMean();
        geometry_msgs::Point p;
        p.x=mean(0);
        p.y=mean(1);
        p.z=mean(2);
        static_object_marker.points.push_back(p);

        //push another point for the second point of the line
        p.x=mean(0);
        p.y=mean(1);
        p.z=mean(2)+3;
        static_object_marker.points.push_back(p);
      }

    }
    m_static_objectsPublisher.publish (static_object_marker);

    // 		m_algorithm->unlockHypotheses();

  }


  void Tracker::publish_dynamic_hypotheses(){
    std::vector<MultiHypothesisTracker::Hypothesis*> hypotheses = m_algorithm->getHypotheses();
    visualization_msgs::Marker dynamic_object_marker= createMarker(0,0,0 , 0.0, 0.5, 0.5);
    dynamic_object_marker.type = visualization_msgs::Marker::LINE_LIST;
    dynamic_object_marker.color.a = 0.5; // Don't forget to set the alpha!
    double cur= MultiHypothesisTracker::get_time_high_res();

    // Publish tracks
    for(size_t i = 0; i < hypotheses.size(); ++i){
      MultiObjectHypothesis *hypothesis = (MultiObjectHypothesis *) hypotheses[i];

      if (!hypothesis->isStatic() && !hypothesis->is_picked() && (cur - hypothesis->get_born_time() > m_born_time_threshold )    ){
        const vnl_vector<double> &mean = hypothesis->getMean();
        geometry_msgs::Point p;
        p.x=mean(0);
        p.y=mean(1);
        p.z=mean(2);
        dynamic_object_marker.points.push_back(p);

        //push another point for the second point of the line
        p.x=mean(0);
        p.y=mean(1);
        p.z=mean(2)+1.5;
        dynamic_object_marker.points.push_back(p);
      }

    }
    m_dynamic_objectsPublisher.publish (dynamic_object_marker);

    // 		m_algorithm->unlockHypotheses();

  }


  void Tracker::publish_picked_objects(){
    visualization_msgs::Marker picked_object_marker= createMarker();
    picked_object_marker.type = visualization_msgs::Marker::LINE_LIST;
    picked_object_marker.color.a = 0.5; // Don't forget to set the alpha!
    picked_object_marker.color.r = 0.0;
    picked_object_marker.color.g = 0.0;
    picked_object_marker.color.b = 0.0;
    picked_object_marker.lifetime= ros::Duration(1.0);




    //push in the static objects
    for (size_t i = 0; i < m_picked_static_positions.size(); i++) {
      geometry_msgs::Point p;
      p.x=m_picked_static_positions[i].pos(0);
      p.y=m_picked_static_positions[i].pos(1);
      p.z=m_picked_static_positions[i].pos(2);
      picked_object_marker.points.push_back(p);

      //push anothe point for the second point of the line
      p.x=m_picked_static_positions[i].pos(0);
      p.y=m_picked_static_positions[i].pos(1);
      p.z=m_picked_static_positions[i].pos(2)-3;
      picked_object_marker.points.push_back(p);
    }


    //publish the dynamic ones
    std::vector<MultiHypothesisTracker::Hypothesis*> hypotheses = m_algorithm->getHypotheses();
    for(size_t i = 0; i < hypotheses.size(); ++i){
      MultiObjectHypothesis *hypothesis = (MultiObjectHypothesis *) hypotheses[i];

      if (hypothesis->is_picked()){
        const vnl_vector<double> &mean = hypothesis->getMean();
        geometry_msgs::Point p;
        p.x=mean(0);
        p.y=mean(1);
        p.z=mean(2);
        picked_object_marker.points.push_back(p);

        //push another point for the second point of the line
        p.x=mean(0);
        p.y=mean(1);
        p.z=mean(2)-3;
        picked_object_marker.points.push_back(p);
      }

    }
    m_picked_objectsPublisher.publish (picked_object_marker);

  }


  void Tracker::publish_debug(){

    //Publish the data from the first hypothesis and it's latest measurement that it was corrected with
    std::vector<MultiHypothesisTracker::Hypothesis*> hypotheses = m_algorithm->getHypotheses();
    if (hypotheses.empty()){
      return;
    }
    if (hypotheses[0]->get_latest_measurement_time()==0){
      return;
    }

    MultiObjectHypothesis *hypothesis = (MultiObjectHypothesis *) hypotheses[0];


    multi_object_tracking::DebugTracking debug_msg;
    debug_msg.header.seq=m_debug_counter++;
    debug_msg.header.stamp=ros::Time::now();
    debug_msg.header.frame_id=m_world_frame;


    debug_msg.velocity.x=hypothesis->get_velocity()(0);
    debug_msg.velocity.y=hypothesis->get_velocity()(1);
    debug_msg.velocity.z=hypothesis->get_velocity()(2);
    debug_msg.velocity_norm=hypothesis->get_velocity().two_norm();
    debug_msg.position.x=hypothesis->getMean()(0);
    debug_msg.position.y=hypothesis->getMean()(1);
    debug_msg.position.z=hypothesis->getMean()(2);
    debug_msg.drone_tilt_angle=hypothesis->get_latest_measurement().rotation_angle;

    m_debug_publisher.publish (debug_msg);
  }


  bool Tracker::tracking_reset(std_srvs::Empty::Request &req, std_srvs::Empty::Response &res){
    std::cout << "received tracking reset" << '\n';
    m_picked_static_positions.clear();
    std::vector<MultiHypothesisTracker::Hypothesis*> hypotheses = m_algorithm->getHypotheses();
    for(size_t i = 0; i < hypotheses.size(); ++i){
      MultiObjectHypothesis *hypothesis = (MultiObjectHypothesis *) hypotheses[i];
      hypothesis->set_picked(false);
    }
    return true;
  }



}


int main( int argc, char** argv ) {
  ros::init( argc, argv, "multi_object_tracking" );
  ros::NodeHandle n;
  ros::Rate loopRate( 30 );

  MultiObjectTracker::Tracker tracker;

  while( n.ok() ) {
    tracker.update();
    ros::spinOnce();
    tracker.publishHypotheses();
    tracker.publish_hypotheses_future();
    tracker.publish_hypotheses_box_msg();
    tracker.publish_hypotheses_future_box_msg();
    tracker.publish_hypothesis_covariance();
    tracker.publish_static_hypotheses();
    tracker.publish_dynamic_hypotheses();
    tracker.publish_picked_objects();
    // tracker.publish_debug();
    loopRate.sleep();

  }




  return 0;
}