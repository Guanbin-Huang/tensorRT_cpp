#ifndef ARCFACE_HPP
#define ARCFACE_HPP

#include <vector>
#include <memory>
#include <string>
#include <future>
#include <opencv2/opencv.hpp>

namespace Arcface{

    using namespace std;

    struct landmarks{
        float points[10];
    };

    typedef cv::Mat_<float>           feature;
    typedef tuple<cv::Mat, landmarks> commit_input;

    class Infer{
    public:
        virtual shared_future<feature>         commit (const commit_input& image)          = 0;
        virtual vector<shared_future<feature>> commits(const vector<commit_input>& images) = 0;
    };

    // RAII，如果创建失败，返回空指针
    shared_ptr<Infer> create_infer(const string& engine_file, int gpuid=0);

}; // namespace RetinaFace

#endif // ARCFACE_HPP