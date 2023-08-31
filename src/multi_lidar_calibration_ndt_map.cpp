#include <multi_lidar_calibration/multi_lidar_calibration_ndt_map.hpp>

namespace calibration
{
namespace multi_lidar_calibration_ndt_map
{

MultiLidarCalibrationNdtMap::MultiLidarCalibrationNdtMap()
: Node("multi_lidar_calibration_ndt_map")
{
  // initialize parameters of node
  param_.pcd_path = declare_parameter<std::string>("pcd_path", " ");
  param_.initial_pose =
    declare_parameter<std::vector<double>>("initial_pose", {0.0, 0.0, 0.0, 0.0, 1.57, 0.0});
  param_.leaf_size = declare_parameter<double>("leaf_size", 0.1);
  param_.max_iteration = declare_parameter<int>("max_iteration", 100);
  param_.transform_epsilon = declare_parameter<double>("transform_epsilon", 0.01);
  param_.step_size = declare_parameter<double>("step_size", 0.1);
  param_.resolution = declare_parameter<double>("resolution", 0.5);

  // sign
  is_source_pt_set_ = false;

  // load pcd file
  // source_pointcloud_ = new pcl::PointCloud<pcl::PointXYZI>;
  pcl::io::loadPCDFile<pcl::PointXYZI>(param_.pcd_path, source_pointcloud_);

  pointcloud_target_sub_ = create_subscription<sensor_msgs::msg::PointCloud2>(
    "~/input/target_pointcloud", rclcpp::SensorDataQoS(),
    std::bind(&MultiLidarCalibrationNdtMap::callbackLidar, this, std::placeholders::_1));

  approximate_voxel_filter_.setLeafSize(param_.leaf_size, param_.leaf_size, param_.leaf_size);

  ndt_.setMaximumIterations(param_.max_iteration);
  ndt_.setTransformationEpsilon(param_.transform_epsilon);
  ndt_.setStepSize(param_.step_size);
  ndt_.setResolution(param_.resolution);

  Eigen::Translation3f initial_translation(
    param_.initial_pose.at(0), param_.initial_pose.at(1), param_.initial_pose.at(2));

  Eigen::AngleAxisf initial_rotation_x(param_.initial_pose.at(3), Eigen::Vector3f::UnitX());
	Eigen::AngleAxisf initial_rotation_y(param_.initial_pose.at(4), Eigen::Vector3f::UnitY());
	Eigen::AngleAxisf initial_rotation_z(param_.initial_pose.at(5), Eigen::Vector3f::UnitZ());
  current_transform_mtraix_ =
    (initial_translation * initial_rotation_x * initial_rotation_y * initial_rotation_z)
      .matrix();
  std::cout << "initial guess: " << std::endl << current_transform_mtraix_ << std::endl;

  // tf2 broadcaster
  tf_broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(*this);

}

MultiLidarCalibrationNdtMap::~MultiLidarCalibrationNdtMap()
{
}

void MultiLidarCalibrationNdtMap::callbackLidar(const sensor_msgs::msg::PointCloud2::ConstSharedPtr & msg)
{
  rclcpp::Time start_time = this->now();
  
  pcl::PointCloud<pcl::PointXYZI>::Ptr target_pointcloud (new pcl::PointCloud<pcl::PointXYZI>);
  pcl::PointCloud<pcl::PointXYZI>::Ptr final_pointcloud (new pcl::PointCloud<pcl::PointXYZI>);

  // pcl::io::loadPCDFile<pcl::PointXYZI>(param_.pcd_path, *source_pointcloud);
  pcl::PointCloud<pcl::PointXYZI>::Ptr filtered_source_pointcloud(
    new pcl::PointCloud<pcl::PointXYZI>);
  pcl::PointCloud<pcl::PointXYZI>::Ptr filtered_target_pointcloud(
    new pcl::PointCloud<pcl::PointXYZI>);

  pcl::fromROSMsg(*msg, *target_pointcloud);

  approximate_voxel_filter_.setInputCloud(target_pointcloud);
  approximate_voxel_filter_.filter(*filtered_target_pointcloud);

  ndt_.setInputSource(filtered_target_pointcloud);
  if(!is_source_pt_set_)
  {
    ndt_.setInputTarget(std::make_shared<pcl::PointCloud<pcl::PointXYZI>>(source_pointcloud_));
  }

  ndt_.align(*final_pointcloud, current_transform_mtraix_);

  if (ndt_.hasConverged())
  {
    current_transform_mtraix_ = ndt_.getFinalTransformation();
    std::cout << "NDT converged." << std::endl
              << "The score is " << ndt_.getFitnessScore() << std::endl;
    std::cout << "Transformation matrix:" << std::endl;
    std::cout << current_transform_mtraix_ << std::endl;
    Eigen::Matrix3f rotation_matrix = current_transform_mtraix_.block(0, 0, 3, 3);
    Eigen::Vector3f translation_vector = current_transform_mtraix_.block(0, 3, 3, 1);
    std::cout << "This transformation can be replicated using:" << std::endl;
    std::cout << "ros2 run tf2_ros static_transform_publisher " << translation_vector.transpose()
              << " " << rotation_matrix.eulerAngles(2,1,0).transpose() << " " << "map"
              << " " << msg->header.frame_id.c_str() << std::endl;

    Eigen::Quaternionf q(rotation_matrix);
    geometry_msgs::msg::TransformStamped t;
    t.header.stamp = msg->header.stamp;
    t.header.frame_id = "map";
    t.child_frame_id = msg->header.frame_id;
    t.transform.translation.x = translation_vector[0];
    t.transform.translation.y = translation_vector[1];
    t.transform.translation.z = translation_vector[2];
    t.transform.rotation.x = q.x();
    t.transform.rotation.y = q.y();
    t.transform.rotation.z = q.z();
    t.transform.rotation.w = q.w();
    tf_broadcaster_->sendTransform(t);
  }
  rclcpp::Time end_time = this->now();
  std::cout << "process time: " << (end_time - start_time).seconds() * 1000.0 << "ms.\n";
}

} // namespace multi_lidar_calibration_ndt_map
} // calibration 