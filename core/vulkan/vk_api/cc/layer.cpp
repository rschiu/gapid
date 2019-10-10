/*
 * Copyright (C) 2019 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstdio>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <android/log.h>
#include "protos/perfetto/trace/gpu/gpu_render_stage_event.pbzero.h"
#include "protos/perfetto/trace/gpu/vulkan_api_event.pbzero.h"
#include "layer.h"

#include "core/vulkan/vk_api/cc/tracing_helpers.h"



#define LAYER_NAME "VkApi"

#define LAYER_NAME_FUNCTION(fn) VkApi##fn

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LAYER_NAME, __VA_ARGS__);

class GpuRenderStageDataSource : public perfetto::DataSource<GpuRenderStageDataSource> {
 public:
  void OnSetup(const SetupArgs& args) override {
    first = true;
  }

  void OnStart(const StartArgs&) override { LOGI("GpuRenderStageDataSource OnStart called"); }

  void OnStop(const StopArgs&) override { LOGI("GpuRenderStageDataSource OnStop called"); }

  bool first = true;
  uint64_t count = 0;
};

class VkApiDataSource : public perfetto::DataSource<GpuRenderStageDataSource> {
 public:
  void OnSetup(const SetupArgs& args) override { }

  void OnStart(const StartArgs&) override { LOGI("VkApiDataSource OnStart called"); }

  void OnStop(const StopArgs&) override { LOGI("VkApiDataSource OnStop called"); }
};


//PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(GpuRenderStageDataSource);
//PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(VkApiDataSource);

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(GpuRenderStageDataSource);
PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(VkApiDataSource);

namespace vk_api {

Context &GetGlobalContext() {
  // We rely on C++11 static initialization rules here.
  // kContext will get allocated on first use, and freed in the
  // same order (more or less).
  static Context kContext;
  return kContext;
}


namespace {

template <typename T>
struct link_info_traits {
  const static bool is_instance =
      std::is_same<T, const VkInstanceCreateInfo>::value;
  using layer_info_type =
      typename std::conditional<is_instance, VkLayerInstanceCreateInfo,
                                VkLayerDeviceCreateInfo>::type;
  const static VkStructureType sType =
      is_instance ? VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO
                  : VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
};

// Get layer_specific data for this layer.
// Will return either VkLayerInstanceCreateInfo or
// VkLayerDeviceCreateInfo depending on the type of the pCreateInfo
// passed in.
template <typename T>
typename link_info_traits<T>::layer_info_type *get_layer_link_info(
    T *pCreateInfo) {
  using layer_info_type = typename link_info_traits<T>::layer_info_type;

  auto layer_info = const_cast<layer_info_type *>(
      static_cast<const layer_info_type *>(pCreateInfo->pNext));

  while (layer_info) {
    if (layer_info->sType == link_info_traits<T>::sType &&
        layer_info->function == VK_LAYER_LINK_INFO) {
      return layer_info;
    }
    layer_info = const_cast<layer_info_type *>(
        static_cast<const layer_info_type *>(layer_info->pNext));
  }
  return layer_info;
}
}  // namespace

static const VkLayerProperties global_layer_properties[] = {{
    LAYER_NAME,
    VK_VERSION_MAJOR(1) | VK_VERSION_MINOR(0) | 5,
    1,
    "Vk Api",
}};

VkResult get_layer_properties(uint32_t *pPropertyCount,
                              VkLayerProperties *pProperties) {
  if (pProperties == NULL) {
    *pPropertyCount = 1;
    return VK_SUCCESS;
  }

  if (pPropertyCount == 0) {
    return VK_INCOMPLETE;
  }
  *pPropertyCount = 1;
  memcpy(pProperties, global_layer_properties, sizeof(global_layer_properties));
  return VK_SUCCESS;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                   VkLayerProperties *pProperties) {
    LOGI("vkEnumerateInstanceLayerProperties");
  return get_layer_properties(pPropertyCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceLayerProperties(VkPhysicalDevice, uint32_t *pPropertyCount,
                                 VkLayerProperties *pProperties) {
    LOGI("vkEnumerateDeviceLayerProperties");
  return get_layer_properties(pPropertyCount, pProperties);
}

static const VkExtensionProperties device_extensions[] = {{VK_EXT_DEBUG_MARKER_EXTENSION_NAME, VK_EXT_DEBUG_MARKER_SPEC_VERSION}};

// Overload vkEnumerateDeviceExtensionProperties
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
        VkPhysicalDevice physicalDevice, const char *pLayerName,
        uint32_t *pPropertyCount, VkExtensionProperties *pProperties) {
    LOGI("vkEnumerateDeviceExtensionProperties");

    if (pLayerName && !strcmp(pLayerName, LAYER_NAME)) {
        if (pProperties == NULL) {
            *pPropertyCount = 1;
            return VK_SUCCESS;
        }
        if (*pPropertyCount > 0) {
        memcpy(pProperties, device_extensions, sizeof(VkExtensionProperties));
        }
        return VK_SUCCESS;
    }

    if (pProperties == NULL) {
        VkResult res = GetGlobalContext().GetInstance()->vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
        if (res == VK_SUCCESS) {
            (*pPropertyCount)++;
        }
        return res;
    }
    if (*pPropertyCount > 0) {
        GetGlobalContext().GetInstance()->vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
        memcpy(&pProperties[*pPropertyCount-1], device_extensions, sizeof(VkExtensionProperties));
    }
    return VK_SUCCESS;
}


// Overload vkEnumerateInstanceExtensionProperties
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceExtensionProperties(
    const char * pLayerName, uint32_t *pPropertyCount,
    VkExtensionProperties * pProperties) {
  LOGI("vkEnumerateInstanceExtensionProperties");
  if(pPropertyCount) *pPropertyCount = 0;
  return VK_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectNameEXT(
    VkDevice                                    device,
    VkDebugMarkerObjectNameInfoEXT*             pNameInfo) {

  static int cnt = 1;
  LOGI("vkDebugMarkerSetObjectNameEXT");
  VkApiDataSource::Trace([&](VkApiDataSource::TraceContext ctx) {
      LOGI("VkApiDataSource tracing lambda called");
      auto data_source = ctx.GetDataSourceLocked();
      {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(cnt * 10 - 1);
        auto event = packet->set_vk_debug_marker();
        event->set_vk_device(reinterpret_cast<uint64_t>(device));
        event->set_object_type(::perfetto::protos::pbzero::VkDebugMarkerObjectName_VkObjectType(pNameInfo->objectType));
        event->set_object(pNameInfo->object);
        event->set_object_name(pNameInfo->pObjectName);
      }
  });
  GpuRenderStageDataSource::Trace([&](GpuRenderStageDataSource::TraceContext ctx) {
      LOGI("GpuRenderStageDataSource tracing lambda called");
      auto data_source = ctx.GetDataSourceLocked();
      if (data_source->first) {
        data_source->count = 0;
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(0);
        auto event = packet->set_gpu_render_stage_event();
        auto spec = event->set_specifications();
        auto hw_queue = spec->add_hw_queue();
        hw_queue->set_name("queue 0");
        hw_queue = spec->add_hw_queue();
        hw_queue->set_name("queue 1");
        auto stage = spec->add_stage();
        stage->set_name("stage 0");
        stage = spec->add_stage();
        stage->set_name("stage 1");
        stage = spec->add_stage();
        stage->set_name("stage 2");
        packet->Finalize();
        data_source->first = false;
      }
      {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(cnt * 10);
        auto event = packet->set_gpu_render_stage_event();
        event->set_event_id(cnt);
        event->set_duration(5);
        event->set_hw_queue_id(cnt % 2);
        event->set_stage_id(cnt % 3);
        event->set_context(42);
        event->set_render_target_handle(pNameInfo->object);
        cnt++;
      }
  });

  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectTagEXT(
    VkDevice                                    device,
    VkDebugMarkerObjectTagInfoEXT*             pTagInfo) {

  LOGI("vkDebugMarkerSetObjectTagEXT");

  return GetGlobalContext().GetInstance()->vkDebugMarkerSetObjectTagEXT(device, pTagInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectNameEXT(
    VkDevice                                    device,
    const VkDebugUtilsObjectNameInfoEXT*        pNameInfo) {
  LOGI("vkSetDebugUtilsObjectNameEXT");

  return GetGlobalContext().GetInstance()->vkSetDebugUtilsObjectNameEXT(device, pNameInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer                             commandBuffer,
    const VkCommandBufferBeginInfo*             pBeginInfo) {
  LOGI("vkBeginCommandBuffer");

  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerBeginEXT(
    VkCommandBuffer                             commandBuffer,
    VkDebugMarkerMarkerInfoEXT*                 pMarkerInfo) {
}

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerEndEXT(
    VkCommandBuffer                             commandBuffer) {
}

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerInsertEXT(
    VkCommandBuffer                             commandBuffer,
    VkDebugMarkerMarkerInfoEXT*                 pMarkerInfo) {
}

////////////////////////////////////////////////////////////////////////////////////////////////////


// Overload vkCreateInstance. It is all book-keeping
// and passthrough to the next layer (or ICD) in the chain.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) {
  LOGI("vk_api::vkCreateInstance");

  // Start perfetto
  perfetto::TracingInitArgs args;
  args.backends = perfetto::kSystemBackend;
  perfetto::Tracing::Initialize(args);

  {
    perfetto::DataSourceDescriptor dsd;
    dsd.set_name("gpu.renderstages");
    GpuRenderStageDataSource::Register(dsd);
  }
  // DataSourceDescriptor can be used to advertise domain-specific features.
  {
    perfetto::DataSourceDescriptor dsd;
    dsd.set_name("vk_api");

    VkApiDataSource::Register(dsd);
  }


  VkLayerInstanceCreateInfo *layer_info = get_layer_link_info(pCreateInfo);

  // Grab the pointer to the next vkGetInstanceProcAddr in the chain.
  PFN_vkGetInstanceProcAddr get_instance_proc_addr =
      layer_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;

  // From that get the next vkCreateInstance function.
  PFN_vkCreateInstance create_instance = reinterpret_cast<PFN_vkCreateInstance>(
      get_instance_proc_addr(NULL, "vkCreateInstance"));

  if (create_instance == NULL) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  // The next layer may read from layer_info,
  // so advance the pointer for it.
  layer_info->u.pLayerInfo = layer_info->u.pLayerInfo->pNext;

  // Actually call vkCreateInstance, and keep track of the result.
  VkResult result = create_instance(pCreateInfo, pAllocator, pInstance);

  // If it failed, then we don't need to track this instance.
  if (result != VK_SUCCESS) return result;

  PFN_vkEnumerateDeviceExtensionProperties
      enumerate_device_extension_properties =
          reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
              get_instance_proc_addr(*pInstance,
                                     "vkEnumerateDeviceExtensionProperties"));
  if (!enumerate_device_extension_properties) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  InstanceData data;

#define GET_PROC(name) \
  data.name =          \
      reinterpret_cast<PFN_##name>(get_instance_proc_addr(*pInstance, #name))
  GET_PROC(vkGetInstanceProcAddr);
  GET_PROC(vkSetDebugUtilsObjectNameEXT);
  GET_PROC(vkEnumerateDeviceExtensionProperties);
  GET_PROC(vkDebugMarkerSetObjectNameEXT);
  GET_PROC(vkDebugMarkerSetObjectTagEXT);
  GET_PROC(vkCmdDebugMarkerBeginEXT);
  GET_PROC(vkCmdDebugMarkerEndEXT);
  GET_PROC(vkCmdDebugMarkerInsertEXT);

#undef GET_PROC
  // Add this instance, along with the vkGetInstanceProcAddr to our
  // map. This way when someone calls vkGetInstanceProcAddr, we can forward
  // it to the correct "next" vkGetInstanceProcAddr.
  {
    auto instances = GetGlobalContext().GetInstance();
    (*instances) = data;
  }

  return result;
}


// Overload vkCreateDevice. It is all book-keeping
// and passthrough to the next layer (or ICD) in the chain.
VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDevice(VkPhysicalDevice gpu, const VkDeviceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
  LOGI("vk_api::vkCreateDevice");
  VkLayerDeviceCreateInfo *layer_info = get_layer_link_info(pCreateInfo);

  // Grab the fpGetInstanceProcAddr from the layer_info. We will get
  // vkCreateDevice from this.
  // Note: we cannot use our instance_map because we do not have a
  // vkInstance here.
  PFN_vkGetInstanceProcAddr get_instance_proc_addr =
      layer_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;

  PFN_vkCreateDevice create_device = reinterpret_cast<PFN_vkCreateDevice>(
      get_instance_proc_addr(NULL, "vkCreateDevice"));

  if (!create_device) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  // We want to store off the next vkGetDeviceProcAddr so keep track of it now
  // before we advance the pointer.
  PFN_vkGetDeviceProcAddr get_device_proc_addr =
      layer_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;

  // The next layer may read from layer_info,
  // so advance the pointer for it.
  layer_info->u.pLayerInfo = layer_info->u.pLayerInfo->pNext;

  // Actually make the call to vkCreateDevice.
  VkResult result = create_device(gpu, pCreateInfo, pAllocator, pDevice);

  // If we failed, then we don't store the associated pointers.
  if (result != VK_SUCCESS) {
    return result;
  }

  DeviceData data{gpu};

#define GET_PROC(name) \
  data.name =          \
      reinterpret_cast<PFN_##name>(get_device_proc_addr(*pDevice, #name));

  GET_PROC(vkGetDeviceProcAddr);
  GET_PROC(vkDebugMarkerSetObjectNameEXT);
  GET_PROC(vkDebugMarkerSetObjectTagEXT);

#undef GET_PROC

  // Add this device, along with the vkGetDeviceProcAddr to our map.
  // This way when someone calls vkGetDeviceProcAddr, we can forward
  // it to the correct "next" vkGetDeviceProcAddr.
  {
    auto device_map = GetGlobalContext().GetDeviceMap();
    if (device_map->find(*pDevice) != device_map->end()) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }
    (*device_map)[*pDevice] = data;
  }

  return result;
}

// Overload GetDeviceProcAddr.
// We provide an overload of vkDestroyDevice for book-keeping.
// The rest of the overloads are swapchain-specific.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice dev, const char *funcName) {
#define INTERCEPT(func)         \
  if (!strcmp(funcName, #func)) {\
    LOGI("vkGetDeviceProcAddr intercepted: %s", funcName); \
    return reinterpret_cast<PFN_vkVoidFunction>(func); \
  }

  INTERCEPT(vkGetDeviceProcAddr);
  INTERCEPT(vkDebugMarkerSetObjectNameEXT);
  INTERCEPT(vkDebugMarkerSetObjectTagEXT);
  INTERCEPT(vkCmdDebugMarkerBeginEXT);
  INTERCEPT(vkCmdDebugMarkerEndEXT);
  INTERCEPT(vkCmdDebugMarkerInsertEXT);

#undef INTERCEPT

  // If we are calling a non-overloaded function then we have to
  // return the "next" in the chain. On vkCreateDevice we stored this in the
  // map so we can call it here.

  PFN_vkGetDeviceProcAddr device_proc_addr =
      GetGlobalContext().GetDeviceData(dev)->vkGetDeviceProcAddr;
  return device_proc_addr(dev, funcName);
}


// Overload GetInstanceProcAddr.
// It also provides the overloaded function for vkCreateDevice. This way we can
// also hook vkGetDeviceProcAddr.
// Lastly it provides vkDestroyInstance for book-keeping purposes.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *funcName) {
#define INTERCEPT(func)         \
  if (!strcmp(funcName, #func)) { \
    LOGI("vkGetInstanceProcAddr intercepted: %s", funcName); \
  return reinterpret_cast<PFN_vkVoidFunction>(func); \
  }

  INTERCEPT(vkGetInstanceProcAddr);
  INTERCEPT(vkCreateInstance);
  INTERCEPT(vkCreateDevice);

  INTERCEPT(vkDebugMarkerSetObjectNameEXT);
  INTERCEPT(vkSetDebugUtilsObjectNameEXT);
  INTERCEPT(vkDebugMarkerSetObjectTagEXT);
  INTERCEPT(vkEnumerateDeviceLayerProperties);
  INTERCEPT(vkEnumerateDeviceExtensionProperties);
  INTERCEPT(vkEnumerateInstanceLayerProperties);

#undef INTERCEPT

  // If we are calling a non-overloaded function then we have to
  // return the "next" in the chain. On vkCreateInstance we stored this in
  // the map so we can call it here.
  PFN_vkGetInstanceProcAddr instance_proc_addr =
      GetGlobalContext().GetInstance()->vkGetInstanceProcAddr;

  return instance_proc_addr(instance, funcName);
}


}  // namespace vk_api

extern "C" {

// For this to function on Android the entry-point names for GetDeviceProcAddr
// and GetInstanceProcAddr must be ${layer_name}/Get*ProcAddr.
// This is a bit surprising given that we *MUST* also export
// vkEnumerate*Layers without any prefix.
VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
LAYER_NAME_FUNCTION(GetDeviceProcAddr)(VkDevice dev, const char *funcName) {
  return vk_api::vkGetDeviceProcAddr(dev, funcName);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL LAYER_NAME_FUNCTION(
    GetInstanceProcAddr)(VkInstance instance, const char *funcName) {
  return vk_api::vkGetInstanceProcAddr(instance, funcName);
}

// Documentation is sparse for Android, looking at libvulkan.so
// These 4 functions must be defined in order for this to even
// be considered for loading.
#if defined(__ANDROID__)
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                   VkLayerProperties *pProperties) {
  return vk_api::vkEnumerateInstanceLayerProperties(pPropertyCount,
                                                       pProperties);
}

// On Android this must also be defined, even if we have 0
// layers to expose.
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceExtensionProperties(const char *pLayerName,
                                       uint32_t *pPropertyCount,
                                       VkExtensionProperties *pProperties) {
  return vk_api::vkEnumerateInstanceExtensionProperties(
      pLayerName, pPropertyCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
    VkLayerProperties *pProperties) {
  return vk_api::vkEnumerateDeviceLayerProperties(
      physicalDevice, pPropertyCount, pProperties);
}

// On Android this must also be defined, even if we have 0
// layers to expose.
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                     const char *pLayerName,
                                     uint32_t *pPropertyCount,
                                     VkExtensionProperties *pProperties) {
  return vk_api::vkEnumerateDeviceExtensionProperties(
      physicalDevice, pLayerName, pPropertyCount, pProperties);
}
#endif
}
