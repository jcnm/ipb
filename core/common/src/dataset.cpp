#include <ipb/common/dataset.hpp>
#include <algorithm>

namespace ipb {
namespace common {

DataSet::DataSet(const std::vector<DataPoint>& data_points)
    : data_points_(data_points) {
}

// void DataSet::add_data_point(const DataPoint& data_point) {
//     std::lock_guard<std::mutex> lock(mutex_);
//     data_points_.push_back(data_point);
// }

// void DataSet::add_data_points(const std::vector<DataPoint>& data_points) {
//     std::lock_guard<std::mutex> lock(mutex_);
//     data_points_.insert(data_points_.end(), data_points.begin(), data_points.end());
// }

// std::vector<DataPoint> DataSet::get_data_points() const {
//     std::lock_guard<std::mutex> lock(mutex_);
//     return data_points_;
// }

// size_t DataSet::size() const {
//     std::lock_guard<std::mutex> lock(mutex_);
//     return data_points_.size();
// }

// bool DataSet::empty() const {
//     std::lock_guard<std::mutex> lock(mutex_);
//     return data_points_.empty();
// }

// void DataSet::clear() {
//     std::lock_guard<std::mutex> lock(mutex_);
//     data_points_.clear();
//     timestamp_ = std::chrono::steady_clock::now();
// }

// void DataSet::reserve(size_t capacity) {
//     std::lock_guard<std::mutex> lock(mutex_);
//     data_points_.reserve(capacity);
// }

// std::vector<DataPoint> DataSet::filter_by_protocol(const std::string& protocol_id) const {
//     std::lock_guard<std::mutex> lock(mutex_);
//     std::vector<DataPoint> filtered;
    
//     std::copy_if(data_points_.begin(), data_points_.end(), std::back_inserter(filtered),
//         [&protocol_id](const DataPoint& dp) {
//             return dp.get_protocol_id() == protocol_id;
//         });
    
//     return filtered;
// }

// std::vector<DataPoint> DataSet::filter_by_quality(DataQuality quality) const {
//     std::lock_guard<std::mutex> lock(mutex_);
//     std::vector<DataPoint> filtered;
    
//     std::copy_if(data_points_.begin(), data_points_.end(), std::back_inserter(filtered),
//         [quality](const DataPoint& dp) {
//             return dp.get_quality() == quality;
//         });
    
//     return filtered;
// }

// std::chrono::steady_clock::time_point DataSet::get_timestamp() const {
//     return timestamp_;
// }

// void DataSet::set_timestamp(std::chrono::steady_clock::time_point timestamp) {
//     timestamp_ = timestamp;
// }

} // namespace common
} // namespace ipb

