#ifndef GPUIP_CUDA_H_
#define GPUIP_CUDA_H_
//----------------------------------------------------------------------------//
#include "gpuip.h"
#include <cuda.h>
#include <cuda_runtime.h>
//----------------------------------------------------------------------------//
namespace gpuip {
//----------------------------------------------------------------------------//
class CUDAImpl : public Base
{
  public:
    CUDAImpl();

    virtual bool InitBuffers(std::string * err);
    
    virtual bool Build(std::string * err);

    virtual bool Process(std::string * err);
    
    virtual bool Copy(const std::string & buffer,
                      Buffer::CopyOperation op,
                      void * data,
                      std::string * err);

    virtual std::string GetBoilerplateCode(Kernel::Ptr kernel) const;
    
  protected:
    std::vector<CUfunction> _cudaKernels;
    bool _cudaBuild;
    CUmodule _cudaModule;
    std::map<std::string, float*> _cudaBuffers;
    
    bool _LaunchKernel(Kernel & kernel,
                       const CUfunction & cudaKernel,
                       std::string * err);
};
//----------------------------------------------------------------------------//
} // end namespace gpuip

#endif