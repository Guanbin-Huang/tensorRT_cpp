#ifndef FALL_GCN_HPP
#define FALL_GCN_HPP

#include <vector>
#include <memory>
#include <string>
#include <future>
#include <opencv2/opencv.hpp>

namespace FallGCN{

    using namespace std;
    using namespace cv;

    enum class FallState : int{
        Fall      = 0,
        Stand     = 1,
        UnCertain = 2
    };

    const char* state_name(FallState state);

    class Infer{
    public:
        virtual shared_future<tuple<FallState, float>> commit(const vector<Point3f>& keys, const Rect& box) = 0;
    };

    // RAII，如果创建失败，返回空指针
    shared_ptr<Infer> create_infer(const string& engine_file, int gpuid);

}; // namespace AlphaPose

#endif // FALL_GCN_HPP