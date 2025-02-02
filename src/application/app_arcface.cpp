#include <builder/trt_builder.hpp>
#include <infer/trt_infer.hpp>
#include <common/ilogger.hpp>
#include "app_retinaface/retinaface.hpp"
#include "app_arcface/arcface.hpp"
#include "tools/deepsort.hpp"
#include "tools/zmq_remote_show.hpp"

using namespace std;
using namespace cv;

bool requires(const char* name);
bool compile_retinaface(int input_width, int input_height, string& out_model_file);

static bool compile_models(){

    TRT::set_device(0);
    string model_file;

    if(!compile_retinaface(640, 480, model_file))
        return false;

    const char* onnx_files[]{"arcface_iresnet50"};
    for(auto& name : onnx_files){
        if(not requires(name))
            return false;

        string onnx_file = iLogger::format("%s.onnx", name);
        string model_file = iLogger::format("%s.fp32.trtmodel", name);
        int test_batch_size = 1;  // 当你需要修改batch大于1时，请查看yolox.cpp:260行备注
        
        // 动态batch和静态batch，如果你想要弄清楚，请打开http://www.zifuture.com:8090/
        // 找到右边的二维码，扫码加好友后进群交流（免费哈，就是技术人员一起沟通）
        if(not iLogger::exists(model_file)){
            bool ok = TRT::compile(
                TRT::TRTMode_FP32,   // 编译方式有，FP32、FP16、INT8
                {},                         // onnx时无效，caffe的输出节点标记
                test_batch_size,            // 指定编译的batch size
                onnx_file,                  // 需要编译的onnx文件
                model_file,                 // 储存的模型文件
                {},                         // 指定需要重定义的输入shape，这里可以对onnx的输入shape进行重定义
                true                        // 是否采用动态batch维度，true采用，false不采用，使用静态固定的batch size
            );

            if(!ok) return false;
        }
    }
    return true;
}

tuple<Mat, vector<string>> build_library(shared_ptr<RetinaFace::Infer> detector, shared_ptr<Arcface::Infer> arcface){
    
    Mat_<float> features(0, 512);
    vector<string> names;
    auto libs = iLogger::find_files("face/library");
    INFO("Build library, %d images", libs.size());

    for(auto& file : libs){
        auto file_name = iLogger::file_name(file, false);
        Mat image      = imread(file);

        auto faces = detector->commit(image).get();
        if(faces.empty()){
            INFOW("%s no detect face.", file.c_str());
            continue;
        }

        RetinaFace::FaceBox max_face = faces[0];
        if(faces.size() > 1){
            int max_face_index = std::max_element(faces.begin(), faces.end(), [](RetinaFace::FaceBox& face1, RetinaFace::FaceBox& face2){
                return face1.area() < face2.area();
            }) - faces.begin();
            max_face = faces[max_face_index];
        }

        auto& face = max_face;
        auto box   = Rect(face.left, face.top, face.right-face.left, face.bottom-face.top);
        box        = box & Rect(0, 0, image.cols, image.rows);

        if(box.width < 80 or box.height < 80)
            continue;

        if(box.area() == 0){
            INFOE("Invalid box, %d, %d, %d, %d", box.x, box.y, box.width, box.height);
            continue;
        }

        auto crop  = image(box).clone();
        Arcface::landmarks landmarks;
        for(int j = 0; j < 10; ++j)
            landmarks.points[j] = face.landmark[j] - (j % 2 == 0 ? face.left : face.top);

        auto feature     = arcface->commit(make_tuple(crop, landmarks)).get();
        string face_name = file_name;
        features.push_back(feature);
        names.push_back(face_name);

        INFO("New face [%s], %d feature, %.5f", face_name.c_str(), feature.cols, face.confidence);

        rectangle(image, cv::Point(face.left, face.top), cv::Point(face.right, face.bottom), Scalar(0, 255, 0), 2);
        for(int j = 0; j < 5; ++j)
            circle(image, Point(face.landmark[j*2+0], face.landmark[j*2+1]), 3, Scalar(0, 255, 0), -1, 16);
        putText(image, face_name, cv::Point(face.left, face.top), 0, 1, Scalar(0, 255, 0), 1, 16);

        string save_file = iLogger::format("face/library_draw/%s.jpg", file_name.c_str());
        imwrite(save_file, image);
    }
    return make_tuple(features, names);
}

int app_arcface(){

    TRT::set_device(0);
    INFO("===================== test arcface fp32 ==================================");

    if(!compile_models())
        return 0;

    iLogger::rmtree("face/library_draw");
    iLogger::rmtree("face/result");
    iLogger::mkdirs("face/library_draw");
    iLogger::mkdirs("face/result");

    auto detector = RetinaFace::create_infer("mb_retinaface.640x480.fp32.trtmodel", 0, 0.5f);
    auto arcface  = Arcface::create_infer("arcface_iresnet50.fp32.trtmodel", 0);
    auto library  = build_library(detector, arcface);

    auto files    = iLogger::find_files("face/recognize");
    for(auto& file : files){

        auto image  = imread(file);
        auto faces  = detector->commit(image).get();
        vector<string> names(faces.size());
        for(int i = 0; i < faces.size(); ++i){
            auto& face = faces[i];
            auto box   = Rect(face.left, face.top, face.right-face.left, face.bottom-face.top);
            box        = box & Rect(0, 0, image.cols, image.rows);
            auto crop  = image(box).clone();
            
            Arcface::landmarks landmarks;
            for(int j = 0; j < 10; ++j)
                landmarks.points[j] = face.landmark[j] - (j % 2 == 0 ? face.left : face.top);

            auto out          = arcface->commit(make_tuple(crop, landmarks)).get();
            auto scores       = Mat(get<0>(library) * out.t());
            float* pscore     = scores.ptr<float>(0);
            int label         = std::max_element(pscore, pscore + scores.rows) - pscore;
            float match_score = max(0.0f, pscore[label]);
            INFO("%f, %s", match_score, get<1>(library)[label].c_str());

            if(match_score > 0.3f){
                names[i] = iLogger::format("%s[%.3f]", get<1>(library)[label].c_str(), match_score);
            }
        }

        for(int i = 0; i < faces.size(); ++i){
            auto& face = faces[i];

            auto color = Scalar(0, 255, 0);
            if(names[i].empty()){
                color = Scalar(0, 0, 255);
                names[i] = "Unknow";
            }
            
            rectangle(image, cv::Point(face.left, face.top), cv::Point(face.right, face.bottom), color, 3);
            putText(image, names[i], cv::Point(face.left, face.top), 0, 1, color, 1, 16);
        }

        auto save_file = iLogger::format("face/result/%s.jpg", iLogger::file_name(file, false).c_str());
        imwrite(save_file, image);
    }
    INFO("Done");
    return 0;
}

int app_arcface_video(){

    TRT::set_device(0);
    INFO("===================== test arcface fp32 ==================================");

    if(!compile_models())
        return 0;

    iLogger::rmtree("face/library_draw");
    iLogger::rmtree("face/result");
    iLogger::mkdirs("face/library_draw");
    iLogger::mkdirs("face/result");

    auto detector = RetinaFace::create_infer("mb_retinaface.640x480.fp32.trtmodel", 0, 0.5f);
    auto arcface  = Arcface::create_infer("arcface_iresnet50.fp32.trtmodel", 0);
    auto library  = build_library(detector, arcface);
    auto remote_show = create_zmq_remote_show();

    // 这是一段人脸晃来晃去的视频
    VideoCapture cap("exp/WIN_20210425_14_23_24_Pro.mp4");
    Mat image;
    while(cap.read(image)){
        auto faces  = detector->commit(image).get();
        vector<string> names(faces.size());
        for(int i = 0; i < faces.size(); ++i){
            auto& face = faces[i];
            auto box   = Rect(face.left, face.top, face.right-face.left, face.bottom-face.top);
            box        = box & Rect(0, 0, image.cols, image.rows);
            auto crop  = image(box).clone();
            
            Arcface::landmarks landmarks;
            for(int j = 0; j < 10; ++j)
                landmarks.points[j] = face.landmark[j] - (j % 2 == 0 ? face.left : face.top);

            auto out          = arcface->commit(make_tuple(crop, landmarks)).get();
            auto scores       = Mat(get<0>(library) * out.t());
            float* pscore     = scores.ptr<float>(0);
            int label         = std::max_element(pscore, pscore + scores.rows) - pscore;
            float match_score = max(0.0f, pscore[label]);

            if(match_score > 0.3f){
                names[i] = iLogger::format("%s[%.3f]", get<1>(library)[label].c_str(), match_score);
            }
        }

        for(int i = 0; i < faces.size(); ++i){
            auto& face = faces[i];

            auto color = Scalar(0, 255, 0);
            if(names[i].empty()){
                color = Scalar(0, 0, 255);
                names[i] = "Unknow";
            }
            
            rectangle(image, cv::Point(face.left, face.top), cv::Point(face.right, face.bottom), color, 3);
            putText(image, names[i], cv::Point(face.left, face.top - 5), 0, 1, color, 1, 16);
        }

        remote_show->post(image);
    }
    INFO("Done");
    return 0;
}

int app_arcface_tracker(){

    TRT::set_device(0);
    INFO("===================== test arcface fp32 ==================================");

    if(!compile_models())
        return 0;

    auto detector = RetinaFace::create_infer("mb_retinaface.640x480.fp32.trtmodel", 0, 0.5f);
    auto arcface  = Arcface::create_infer("arcface_iresnet50.fp32.trtmodel", 0);
    auto library  = build_library(detector, arcface);

    // 请在本地电脑执行tools/show.py，对接好ip地址连接上这个远程显示服务
    auto remote_show = create_zmq_remote_show();

    // 这是一段人脸晃来晃去的视频
    auto tracker     = DeepSORT::create_tracker();
    VideoCapture cap("exp/WIN_20210425_14_23_24_Pro.mp4");
    Mat image;
    int nframe = 0;
    auto time = iLogger::timestamp_now_float();
    while(cap.read(image)){
        auto faces  = detector->commit(image).get();
        vector<string> names(faces.size());
        vector<DeepSORT::Box> boxes;
        for(int i = 0; i < faces.size(); ++i){
            auto& face     = faces[i];
            auto track_box = DeepSORT::convert_to_box(face);
            auto box   = Rect(face.left, face.top, face.right-face.left, face.bottom-face.top);
            box        = box & Rect(0, 0, image.cols, image.rows);
            auto crop  = image(box).clone();
            
            Arcface::landmarks landmarks;
            for(int j = 0; j < 10; ++j)
                landmarks.points[j] = face.landmark[j] - (j % 2 == 0 ? face.left : face.top);

            track_box.feature = arcface->commit(make_tuple(crop, landmarks)).get();
            Mat scores        = get<0>(library) * track_box.feature.t();
            float* pscore     = scores.ptr<float>(0);
            int label         = std::max_element(pscore, pscore + scores.rows) - pscore;
            float match_score = max(0.0f, pscore[label]);
            boxes.emplace_back(std::move(track_box));

            if(match_score > 0.3f){
                names[i] = iLogger::format("%s[%.3f]", get<1>(library)[label].c_str(), match_score);
            }
        }
        tracker->update(boxes);

        auto final_objects = tracker->get_objects();
        for(int i = 0; i < final_objects.size(); ++i){
            auto& person = final_objects[i];
            if(person->time_since_update() == 0 && person->state() == DeepSORT::State::Confirmed){
                Rect box = DeepSORT::convert_box_to_rect(person->last_position());

                rectangle(image, DeepSORT::convert_box_to_rect(person->predict_box()), Scalar(0, 255, 0), 2);
                rectangle(image, box, Scalar(0, 255, 255), 3);

                auto line = person->trace_line();
                for(int j = 0; j < (int)line.size() - 1; ++j){
                    auto& p = line[j];
                    auto& np = line[j + 1];
                    cv::line(image, p, np, Scalar(255, 128, 60), 2, 16);
                }

                putText(image, iLogger::format("%d", person->id()), Point(box.x, box.y-10), 0, 1, Scalar(0, 0, 255), 2, 16);
           }
        }

        for(int i = 0; i < faces.size(); ++i){
            auto& face = faces[i];

            auto color = Scalar(0, 255, 0);
            if(names[i].empty()){
                color = Scalar(0, 0, 255);
                names[i] = "Unknow";
            }
            
            rectangle(image, cv::Point(face.left, face.top), cv::Point(face.right, face.bottom), color, 3);
            putText(image, names[i], cv::Point(face.left + 30, face.top - 10), 0, 1, color, 2, 16);
        }
        remote_show->post(image);
    }
    INFO("Done");
    return 0;
}