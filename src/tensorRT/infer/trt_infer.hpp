

#ifndef TRT_INFER_HPP
#define TRT_INFER_HPP

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <common/trt_tensor.hpp>

namespace TRT {

	class Infer {
	public:
		// 执行forward推理前，请把数据输入到input中，确保其shape有效
		virtual void     forward(bool sync = true, bool resize_output_batch_same_input = true) = 0;
		virtual int      get_max_batch_size() = 0;
		virtual void     set_stream(CUStream stream) = 0;
		virtual CUStream get_stream() = 0;
		virtual void     synchronize() = 0;
		virtual size_t   get_device_memory_size() = 0;
		virtual bool     is_dynamic_batch_dimension() = 0;
		virtual std::shared_ptr<MixMemory> get_workspace() = 0;
		virtual std::shared_ptr<Tensor>    input (int index = 0) = 0;
		virtual std::shared_ptr<Tensor>    output(int index = 0) = 0;
		virtual std::shared_ptr<Tensor>    tensor(const std::string& name) = 0;
		virtual std::string get_input_name (int index = 0) = 0;
		virtual std::string get_output_name(int index = 0) = 0;
		virtual bool is_output_name(const std::string& name) = 0;
		virtual bool is_input_name (const std::string& name) = 0;
		virtual int  num_output() = 0;
		virtual int  num_input() = 0;
		virtual void print() = 0;
		virtual int  device() = 0;
	};

	struct DeviceMemorySummary {
		size_t total;
		size_t available;
	};

	DeviceMemorySummary get_current_device_summary();
	int get_device_count();
	int get_device();
	
	void set_device(int device_id);
	std::shared_ptr<Infer> load_infer_from_memory(const void* pdata, size_t size);
	std::shared_ptr<Infer> load_infer(const std::string& file);
	bool init_nv_plugins();

};	//TRTInfer


#endif //TRT_INFER_HPP