
#ifndef TRT_TENSOR_HPP
#define TRT_TENSOR_HPP

#include <string>
#include <memory>
#include <vector>
#include <map>

// 如果不想依赖opencv，可以去掉这个定义
#define USE_OPENCV

#ifdef USE_OPENCV
#include <opencv2/opencv.hpp>
#endif // USE_OPENCV

#ifdef HAS_CUDA_HALF
struct __half;
#endif // HAS_CUDA_HALF

struct CUstream_st;

namespace TRT {

    #ifdef HAS_CUDA_HALF
    typedef __half halfloat;
    #endif

    typedef CUstream_st* CUStream;

    enum DataHead{
        DataHead_Init = 0,
        DataHead_InGPU = 1,
        DataHead_InCPU = 2
    };

    #ifdef HAS_CUDA_HALF
    enum class DataType : int {
        dtFloat = 0,
        dtHalfloat = 1
    };
    #else
    enum class DataType : int {
        dtFloat = 0
    };
    #endif

    int data_type_size(DataType dt);

    /**
     * @brief 对GPU/CPU内存进行管理、分配/释放
     */
    class MixMemory {
    public:
        virtual ~MixMemory();
        void* gpu(size_t size);
        void* cpu(size_t size);
        void release_gpu();
        void release_cpu();
        void release_all();

        // 这里的GPU、CPU内存都可以用Host、Device直接访问
        // 这里的GPU内存，使用统一内存管理
        inline void* gpu() const { return gpu_; }

        // 这里的CPU内存，使用Pinned Memory，页锁定内存
        // 所以可以Host、Device访问
        inline void* cpu() const { return cpu_; }

    private:
        void* cpu_ = nullptr;
        size_t cpu_size_ = 0;

        void* gpu_ = nullptr;
        size_t gpu_size_ = 0;
    };

    class Tensor {
    public:
        Tensor(const Tensor& other) = delete;
        Tensor& operator = (const Tensor& other) = delete;

        explicit Tensor(DataType dtType = DataType::dtFloat);
        explicit Tensor(int n, int c, int h, int w, DataType dtType = DataType::dtFloat);
        explicit Tensor(int ndims, const int* dims, DataType dtType = DataType::dtFloat);
        explicit Tensor(const std::vector<int>& dims, DataType dtType = DataType::dtFloat);
        virtual ~Tensor();

        int numel();
        int ndims(){return shape_.size();}
        inline int size(int index)  {return shape_[index];}
        inline int shape(int index) {return shape_[index];}

        // 定义基于BCHW的获取方式，请自行保证shape是对应的
        inline int batch()  {return shape_[0];}
        inline int channel(){return shape_[1];}
        inline int height() {return shape_[2];}
        inline int width()  {return shape_[3];}

        inline DataType type()                const { return dtype_; }
        inline const std::vector<int>& dims() const { return shape_; }
        inline int bytes()                    const { return bytes_; }
        inline int bytes(int start_axis)      const { return count(start_axis) * element_size(); }
        inline int element_size()             const { return data_type_size(dtype_); }

        std::shared_ptr<Tensor> clone();
        Tensor& release();
        Tensor& set_to(float value);
        bool empty();

        template<typename ... _Args>
        Tensor& resize(int t, _Args&& ... args){
            resized_dim_.clear();
            return resize_impl(t, args...);
        }

        Tensor& resize(int ndims, const int* dims);
        Tensor& resize(const std::vector<int>& dims);
        Tensor& resize_single_dim(int idim, int size);
        int  count(int start_axis = 0) const;

        Tensor& to_gpu(bool copyedIfCPU = true);
        Tensor& to_cpu(bool copyedIfGPU = true);

        #ifdef HAS_CUDA_HALF
        Tensor& to_half();
        #endif

        Tensor& to_float();
        inline void* cpu() const { ((Tensor*)this)->to_cpu(); return data_->cpu(); }
        inline void* gpu() const { ((Tensor*)this)->to_gpu(); return data_->gpu(); }

        template<typename ... _Args>
        int offset(int t, _Args&& ... args){
            offset_index_.clear();
            return offset_impl(t, args...);
        }

        int offset(const std::vector<int>& index);
        
        template<typename DataT> inline const DataT* cpu() const { return (DataT*)cpu(); }
        template<typename DataT> inline DataT* cpu()             { return (DataT*)cpu(); }

        template<typename DataT, typename ... _Args> 
        inline DataT* cpu(int t, _Args&& ... args) { return cpu<DataT>() + offset(t, args...); }


        template<typename DataT> inline const DataT* gpu() const { return (DataT*)gpu(); }
        template<typename DataT> inline DataT* gpu()             { return (DataT*)gpu(); }

        template<typename DataT, typename ... _Args> 
        inline DataT* gpu(int t, _Args&& ... args) { return gpu<DataT>() + offset(t, args...); }


        template<typename DataT, typename ... _Args> 
        inline DataT& at(int t, _Args&& ... args) { return *(cpu<DataT>() + offset(t, args...)); }
        
        std::shared_ptr<MixMemory> get_data()                    {return data_;}
        std::shared_ptr<MixMemory> get_workspace()               {return workspace_;}
        Tensor& set_workspace(std::shared_ptr<MixMemory> workspace) {workspace_ = workspace;}

        CUStream get_stream(){return stream_;}
        Tensor& set_stream(CUStream stream){stream_ = stream;}

        #ifdef USE_OPENCV
        Tensor& set_mat     (int n, const cv::Mat& image);
        Tensor& set_norm_mat(int n, const cv::Mat& image, float mean[3], float std[3]);
        cv::Mat at_mat(int n = 0, int c = 0) { return cv::Mat(height(), width(), CV_32F, cpu<float>(n, c)); }
        #endif // USE_OPENCV

        Tensor& synchronize();
        const char* shape_string() const{return shape_string_;}

        Tensor& copy_from_gpu(size_t offset, const void* src, size_t num_element);
        Tensor& copy_from_cpu(size_t offset, const void* src, size_t num_element);

        /**
        
        # 以下代码是python中加载Tensor
        import numpy as np

        def load_tensor(file):
            
            with open(file, "rb") as f:
                binary_data = f.read()

            magic_number, ndims, dtype = np.frombuffer(binary_data, np.uint32, count=3, offset=0)
            assert magic_number == 0xFCCFE2E2, f"{file} not a tensor file."
            
            dims = np.frombuffer(binary_data, np.uint32, count=ndims, offset=3 * 4)

            if dtype == 0:
                np_dtype = np.float32
            elif dtype == 1:
                np_dtype = np.float16
            else:
                assert False, f"Unsupport dtype = {dtype}, can not convert to numpy dtype"
                
            return np.frombuffer(binary_data, np_dtype, offset=(ndims + 3) * 4).reshape(*dims)

         **/
        bool save_to_file(const std::string& file);

    private:
        Tensor& resize_impl(int value){
            resized_dim_.push_back(value);
            return resize(resized_dim_);
        }

        template<typename ... _Args>
        Tensor& resize_impl(int t, _Args&& ... args){
            resized_dim_.push_back(t);
            return resize_impl(args...);
        }

        int offset_impl(int value){
            offset_index_.push_back(value);
            return offset(offset_index_);
        }

        template<typename ... _Args>
        int offset_impl(int t, _Args&& ... args){
            offset_index_.push_back(t);
            return offset_impl(args...);
        }

        Tensor& compute_shape_string();
        Tensor& adajust_memory_by_update_dims_or_type();

    private:
        std::vector<int> resized_dim_, offset_index_;
        std::vector<int> shape_;
        size_t capacity_ = 0;
        size_t bytes_    = 0;
        DataHead head_   = DataHead_Init;
        DataType dtype_  = DataType::dtFloat;
        CUStream stream_ = nullptr;
        char shape_string_[100];
        std::shared_ptr<MixMemory> data_;
        std::shared_ptr<MixMemory> workspace_;
    };
};

#endif // TRT_TENSOR_HPP