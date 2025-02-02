#ifndef ALPHA_POSE_HPP
#define ALPHA_POSE_HPP

#include <vector>
#include <memory>
#include <string>
#include <future>
#include <opencv2/opencv.hpp>

namespace AlphaPose{

    using namespace std;
    using namespace cv;

    class Infer{
    public:
        virtual shared_future<vector<Point3f>> commit(const Mat& image, const Rect& box) = 0;
    };

    // RAII，如果创建失败，返回空指针
    shared_ptr<Infer> create_infer(const string& engine_file, int gpuid);

}; // namespace AlphaPose

#endif // ALPHA_POSE_HPP