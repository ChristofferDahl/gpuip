#include "cuda.h"
#include "cuda_error.h"
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <sstream>
//----------------------------------------------------------------------------//
namespace gpuip {
//----------------------------------------------------------------------------//
inline int _cudaGetMaxGflopsDeviceId();
//----------------------------------------------------------------------------//
Base * CreateCUDA()
{
    return new CUDAImpl();
}
//----------------------------------------------------------------------------//
CUDAImpl::CUDAImpl()
        : Base(CUDA), _cudaBuild(false)
{
    if (cudaSetDevice(_cudaGetMaxGflopsDeviceId()) != cudaSuccess) {
        throw std::logic_error("gpuip::CUDAImpl() could not set device id");
    };
    cudaFree(0); //use runtime api to create a CUDA context automatically
}
//----------------------------------------------------------------------------//
bool CUDAImpl::InitBuffers(std::string * err)
{
    cudaError_t c_err;

    std::map<std::string, float*>::iterator itb;
    for(itb = _cudaBuffers.begin(); itb != _cudaBuffers.end(); ++itb) {
        c_err = cudaFree(itb->second);
        if (_cudaErrorFree(c_err, err)) {
            return false;
        }
    }
    _cudaBuffers.clear();
        
    std::map<std::string,Buffer>::const_iterator it;
    for(it = _buffers.begin(); it != _buffers.end(); ++it) {
        _cudaBuffers[it->second.name] = NULL;
        c_err = cudaMalloc(&_cudaBuffers[it->second.name],
                           _GetBufferSize(it->second));
        if(_cudaErrorMalloc(c_err, err)) {
            return false;
        }
    }
    return true;
}
//----------------------------------------------------------------------------//
bool CUDAImpl::Build(std::string * err)
{
    CUresult c_err;
    if (_cudaBuild) {
        _cudaKernels.clear();
        _cudaBuild = false;
        c_err = cuModuleUnload(_cudaModule);
        if (_cudaErrorLoadModule(c_err, err)) {
            return false;
        }
    }
    
    // Create temporary file to compile
    std::ofstream out(".temp.cu");
    out << "extern \"C\" { \n"; // To avoid function name mangling 
    for(size_t i = 0; i < _kernels.size(); ++i) {
        out << _kernels[i]->code << "\n";
    }
    out << "}"; // End the extern C bracket
    out.close();

    int nvcc_exit_status = system(
        "nvcc -ptx .temp.cu -o .temp.ptx --Wno-deprecated-gpu-targets");

    // Cleanup temp text file
    system("rm .temp.cu");
    
    if (nvcc_exit_status) {
        (*err) = "Cuda error: Could not compile kernels.";
        return false;
    }

    // Load cuda ptx from file
    c_err = cuModuleLoad(&_cudaModule, ".temp.ptx");
    system("rm .temp.ptx");
    if (_cudaErrorLoadModule(c_err, err)) {
        return false;
    }

    _cudaKernels.resize(_kernels.size());
    for(size_t i = 0; i < _kernels.size(); ++i) {
        c_err = cuModuleGetFunction(&_cudaKernels[i], _cudaModule,
                                    _kernels[i]->name.c_str());
        if (_cudaErrorGetFunction(c_err, err, _kernels[i]->name)) {
            return false;
        }
    }

    _cudaBuild = true;
    
    return true;
}
//----------------------------------------------------------------------------//
bool CUDAImpl::Process(std::string * err)
{
    for(size_t i = 0; i < _kernels.size(); ++i) {
        if (!_LaunchKernel(*_kernels[i].get(), _cudaKernels[i], err)) {
            return false;
        }
    }
    return true;
}
//----------------------------------------------------------------------------//
bool CUDAImpl::Copy(const std::string & buffer,
                    Buffer::CopyOperation op,
                    void * data,
                    std::string * err)
{
    cudaError_t e;
    const size_t size = _GetBufferSize(_buffers[buffer]);
    if (op == Buffer::READ_DATA) {
        e =cudaMemcpy(data, _cudaBuffers[buffer], size, cudaMemcpyDeviceToHost);
    } else if (op == Buffer::WRITE_DATA) {
        e = cudaMemcpy(_cudaBuffers[buffer],data, size, cudaMemcpyHostToDevice);
    }
    if (_cudaErrorCopy(e, err, buffer, op)) {
        return false;
    }
    return true;
}
//----------------------------------------------------------------------------//
inline std::string _GetTypeStr(const Buffer & buffer)
{
    std::stringstream type;
    switch(buffer.bpp/buffer.channels) {
        case 1:
            if (buffer.channels > 1) {
                type << "uchar";
            } else {
                type << "unsigned char";
            }
            break;
        case 4:
            type << "float";
            break;
        case 8:
            type << "double";
            break;
        default:
            type << "float";
    };
    if (buffer.channels > 1) {
        type << buffer.channels;
    }
    return type.str();
}
std::string CUDAImpl::GetBoilerplateCode(Kernel::Ptr kernel) const 
{
   std::stringstream ss;

   // Indent string
   ss << ",\n" << std::string(kernel->name.size() + 1, ' ');
   const std::string indent = ss.str();
   ss.str("");
   
   ss << "__global__ void\n" << kernel->name << "(";

   bool first = true;

   for(size_t i = 0; i < kernel->inBuffers.size(); ++i) {
       ss << (first ? "" : indent);
       first = false;
       const std::string & bname = kernel->inBuffers[i].first.name;
       ss << "const " << _GetTypeStr(_buffers.find(bname)->second)
          << " * " << kernel->inBuffers[i].second;
   }

   for(size_t i = 0; i < kernel->outBuffers.size(); ++i) {
       ss << (first ? "" : indent);
       first = false;
       const std::string & bname = kernel->outBuffers[i].first.name;
       ss <<  _GetTypeStr(_buffers.find(bname)->second)
          << " * " << kernel->outBuffers[i].second;
   }

   for(size_t i = 0; i < kernel->paramsInt.size(); ++i) {
       ss << (first ? "" : indent);
       first = false;        
       ss << "const int " << kernel->paramsInt[i].name;
   }
   for(size_t i = 0; i < kernel->paramsFloat.size(); ++i) {
       ss << (first ? "" : indent);
       first = false;
       ss << "const float " << kernel->paramsFloat[i].name;
   }
   ss << indent << "const int width" << indent << "const int height)\n";
  
   ss << "{\n";
   ss << "    const int x = blockIdx.x * blockDim.x + threadIdx.x;\n";
   ss << "    const int y = blockIdx.y * blockDim.y + threadIdx.y;\n\n";
   ss << "    // array index\n";
   ss << "    const int idx = x + width * y;\n\n";
   ss << "    // inside image bounds check\n";
   ss << "    if (x >= width || y >= height) {\n";
   ss << "        return;\n";
   ss << "    }\n\n";
   ss << "    // kernel code\n";

   for(size_t i = 0; i < kernel->outBuffers.size(); ++i) {
       const Buffer & b = _buffers.find(
           kernel->outBuffers[i].first.name)->second;
       ss << "    " << kernel->outBuffers[i].second << "[idx] = ";
       if (b.channels == 1) {
           ss << "0;\n";
       } else {
           ss << "make_" << _GetTypeStr(b) << "(";
           for(size_t j = 0; j < b.channels; ++j) {
               ss << (j ==0 ? "" : ", ") << "0";
           }
           ss << ");\n";
       }
   }    
   ss << "}";
   return ss.str();
}
//----------------------------------------------------------------------------//
bool CUDAImpl::_LaunchKernel(Kernel & kernel,
                             const CUfunction & cudaKernel,
                             std::string * err)
{
    // Set CUDA kernel arguments
    CUresult c_err;
    int paramOffset = 0;
    for(size_t i = 0; i < kernel.inBuffers.size(); ++i) {
        c_err = cuParamSetv(cudaKernel, paramOffset,
                            &_cudaBuffers[kernel.inBuffers[i].first.name],
                            sizeof(void*));
        paramOffset += sizeof(void *);
    }
    for(size_t i = 0; i < kernel.outBuffers.size(); ++i) {
        c_err = cuParamSetv(cudaKernel, paramOffset,
                            &_cudaBuffers[kernel.outBuffers[i].first.name],
                            sizeof(void*));
        paramOffset += sizeof(void *);
    }
    for(size_t i = 0; i < kernel.paramsInt.size(); ++i) {
        c_err = cuParamSetv(cudaKernel, paramOffset,
                            &kernel.paramsInt[i].value, sizeof(int));
        paramOffset += sizeof(int);
    }
    for(size_t i = 0; i < kernel.paramsFloat.size(); ++i) {
        c_err = cuParamSetv(cudaKernel, paramOffset,
                            &kernel.paramsFloat[i].value, sizeof(float));
        paramOffset += sizeof(float);
    }
    // int and width parameters
    c_err = cuParamSetv(cudaKernel, paramOffset, &_w, sizeof(int));
    paramOffset += sizeof(int);
    c_err = cuParamSetv(cudaKernel, paramOffset, &_h, sizeof(int));
    paramOffset += sizeof(int);
    
    // It should be fine to check once all the arguments have been set
    if(_cudaErrorCheckParamSet(c_err, err, kernel.name)) {
        return false;
    }
    
    c_err = cuParamSetSize(cudaKernel, paramOffset);
    if (_cudaErrorParamSetSize(c_err, err, kernel.name)) {
        return false;
    }

    // Launch the CUDA kernel
    const int nBlocksHor = _w / 16 + 1;
    const int nBlocksVer = _h / 16 + 1;
    cuFuncSetBlockShape(cudaKernel, 16, 16, 1);
    c_err = cuLaunchGrid(cudaKernel, nBlocksHor, nBlocksVer);
    if (_cudaErrorLaunchKernel(c_err, err, kernel.name)) {
        return false;
    }
        
    return true;
}
//----------------------------------------------------------------------------//
int _cudaGetMaxGflopsDeviceId()
{
	int device_count = 0;
	cudaGetDeviceCount( &device_count );

	cudaDeviceProp device_properties;
	int max_gflops_device = 0;
	int max_gflops = 0;
	
	int current_device = 0;
	cudaGetDeviceProperties( &device_properties, current_device );
	max_gflops = device_properties.multiProcessorCount *
            device_properties.clockRate;
	++current_device;

	while( current_device < device_count )
	{
		cudaGetDeviceProperties( &device_properties, current_device );
		int gflops = device_properties.multiProcessorCount *
                device_properties.clockRate;
		if( gflops > max_gflops )
		{
			max_gflops        = gflops;
			max_gflops_device = current_device;
		}
		++current_device;
	}

	return max_gflops_device;
}
//----------------------------------------------------------------------------//
} // end namespace gpuip
//----------------------------------------------------------------------------//