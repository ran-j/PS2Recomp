#include "gs_gl_interop.h"
#include "context.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#define GL_CALL __stdcall
#else
#include <unistd.h>
#define GL_CALL
#endif

namespace
{
    constexpr unsigned Texture2D = 0x0DE1, ShaderRead = 0x9591, General = 0x958D;
    constexpr auto MemoryHandle = Vulkan::ExternalHandle::get_opaque_memory_handle_type();
    constexpr auto SemaphoreHandle = Vulkan::ExternalHandle::get_opaque_semaphore_handle_type();
    constexpr VkImageUsageFlags ImageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    [[noreturn]] void fail(const std::string &message)
    {
        throw std::runtime_error("parallel-gs interop: " + message);
    }
    template <typename T>
    T load(const char *name)
    {
        auto proc = glfwGetProcAddress(name);
        if (!proc)
            fail(std::string("missing OpenGL entry point ") + name);
        return reinterpret_cast<T>(proc);
    }

    struct GL
    {
#define GL_FN(ret, name, args)              \
    using name##Proc = ret(GL_CALL *) args; \
    name##Proc name = load<name##Proc>("gl" #name)
        GL_FN(void, GetIntegerv, (unsigned, int *));
        GL_FN(void, GetUnsignedBytei_vEXT, (unsigned, unsigned, unsigned char *));
        GL_FN(unsigned, GetError, ());
        GL_FN(void, CreateMemoryObjectsEXT, (int, unsigned *));
        GL_FN(void, DeleteMemoryObjectsEXT, (int, const unsigned *));
        GL_FN(void, MemoryObjectParameterivEXT, (unsigned, unsigned, const int *));
        GL_FN(void, GenTextures, (int, unsigned *));
        GL_FN(void, DeleteTextures, (int, const unsigned *));
        GL_FN(void, BindTexture, (unsigned, unsigned));
        GL_FN(void, TexParameteri, (unsigned, unsigned, int));
        GL_FN(void, TexStorageMem2DEXT, (unsigned, int, unsigned, int, int, unsigned, uint64_t));
        GL_FN(void, GenSemaphoresEXT, (int, unsigned *));
        GL_FN(void, DeleteSemaphoresEXT, (int, const unsigned *));
        GL_FN(void, WaitSemaphoreEXT, (unsigned, unsigned, const unsigned *, unsigned, const unsigned *, const unsigned *));
        GL_FN(void, SignalSemaphoreEXT, (unsigned, unsigned, const unsigned *, unsigned, const unsigned *, const unsigned *));
        GL_FN(void, Flush, ());
        GL_FN(void, Finish, ());
#ifdef _WIN32
        GL_FN(void, ImportMemoryWin32HandleEXT, (unsigned, uint64_t, unsigned, void *));
        GL_FN(void, ImportSemaphoreWin32HandleEXT, (unsigned, unsigned, void *));
#else
        GL_FN(void, ImportMemoryFdEXT, (unsigned, uint64_t, unsigned, int));
        GL_FN(void, ImportSemaphoreFdEXT, (unsigned, unsigned, int));
#endif
#undef GL_FN
        void check(const char *operation)
        {
            auto error = GetError();
            if (error)
                fail(std::string(operation) + " (GL error " + std::to_string(error) + ")");
        }
        void import(unsigned object, Vulkan::ExternalHandle handle, uint64_t size = 0)
        {
            if (!handle)
                fail("could not export Vulkan handle");
#ifdef _WIN32
            if (size)
                ImportMemoryWin32HandleEXT(object, size, 0x9587, handle.handle);
            else
                ImportSemaphoreWin32HandleEXT(object, 0x9587, handle.handle);
            CloseHandle(handle.handle); // Win32 imports retain their own reference.
#else
            if (size)
                ImportMemoryFdEXT(object, size, 0x9586, handle.handle);
            else
                ImportSemaphoreFdEXT(object, 0x9586, handle.handle);
            // A successful fd import transfers ownership to OpenGL.
            auto error = GetError();
            if (error)
            {
                close(handle.handle);
                fail("fd import failed (GL error " + std::to_string(error) + ")");
            }
#endif
            check("external object import");
        }
    };

    struct DeviceState
    {
        GL gl;
        Vulkan::Context context;
        Vulkan::Device device;
        DeviceState()
        {
            if (!Vulkan::Context::init_loader(nullptr))
                fail("Vulkan loader unavailable");

            context.set_num_thread_indices(1);
            if (!context.init_instance(nullptr, 0))
                fail("Vulkan instance initialization failed");

            int uuidCount = 0;
            gl.GetIntegerv(0x9596, &uuidCount); // GL_NUM_DEVICE_UUIDS_EXT
            std::vector<std::array<unsigned char, VK_UUID_SIZE>> uuids(uuidCount);

            for (int i = 0; i < uuidCount; ++i)
                gl.GetUnsignedBytei_vEXT(0x9597, i, uuids[i].data());

            gl.check("device UUID query");
            uint32_t count = 0;
            vkEnumeratePhysicalDevices(context.get_instance(), &count, nullptr);
            std::vector<VkPhysicalDevice> devices(count);
            vkEnumeratePhysicalDevices(context.get_instance(), &count, devices.data());
            VkPhysicalDevice selected = VK_NULL_HANDLE;

            for (auto gpu : devices)
            {
                VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
                VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id};
                vkGetPhysicalDeviceProperties2(gpu, &props);
                for (const auto &uuid : uuids)
                    if (memcmp(uuid.data(), id.deviceUUID, VK_UUID_SIZE) == 0)
                        selected = gpu;
            }

            if (!selected)
                fail("no Vulkan device matches the OpenGL device UUID");

            VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
            external.handleType = MemoryHandle;

            VkPhysicalDeviceImageFormatInfo2 format{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, &external};
            format.format = VK_FORMAT_R8G8B8A8_UNORM;
            format.type = VK_IMAGE_TYPE_2D;
            format.tiling = VK_IMAGE_TILING_OPTIMAL;
            format.usage = ImageUsage;
            VkExternalImageFormatProperties externalProps{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
            VkImageFormatProperties2 props{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, &externalProps};
            if (vkGetPhysicalDeviceImageFormatProperties2(selected, &format, &props) != VK_SUCCESS ||
                !(externalProps.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT))
                fail("RGBA8 external image memory is not exportable");
            VkPhysicalDeviceExternalSemaphoreInfo semInfo{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO};
            semInfo.handleType = SemaphoreHandle;
            VkExternalSemaphoreProperties semProps{VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
            vkGetPhysicalDeviceExternalSemaphoreProperties(selected, &semInfo, &semProps);
            if (!(semProps.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT))
                fail("binary external semaphores are not exportable");

            // Granite enables the platform external-object extensions when available.
            if (!context.init_device(selected, VK_NULL_HANDLE, nullptr, 0))
                fail("Vulkan device initialization failed");

            device.set_context(context);
            device.init_frame_contexts(2);
            device.next_frame_context();
        }
    };

    struct Slot final : GSGpuFrame
    {
        std::shared_ptr<DeviceState> owner;
        Vulkan::ImageHandle image;
        Vulkan::Semaphore ready, released;
        unsigned texture = 0, memory = 0, glReady = 0, glReleased = 0;
        bool produced = false, pendingReady = false, pendingRelease = false, acquired = false;

        explicit Slot(std::shared_ptr<DeviceState> state) : owner(std::move(state))
        {
        }

        Vulkan::ExternalHandle exportSemaphore(const Vulkan::Semaphore &semaphore)
        {
            // Opaque handles share the permanent payload, so export before the first
            // signal is legal. Granite's helper also caters for SYNC_FD and forbids it.
            auto &d = owner->device;
            Vulkan::ExternalHandle handle;
#ifdef _WIN32
            VkSemaphoreGetWin32HandleInfoKHR info{VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR};
            info.semaphore = semaphore->get_semaphore();
            info.handleType = SemaphoreHandle;
            if (d.get_device_table().vkGetSemaphoreWin32HandleKHR(d.get_device(), &info, &handle.handle) != VK_SUCCESS)
                fail("could not export Win32 semaphore");
#else
            VkSemaphoreGetFdInfoKHR info{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
            info.semaphore = semaphore->get_semaphore();
            info.handleType = SemaphoreHandle;
            if (d.get_device_table().vkGetSemaphoreFdKHR(d.get_device(), &info, &handle.handle) != VK_SUCCESS)
                fail("could not export semaphore fd");
#endif
            return handle;
        }

        void initialize(unsigned width, unsigned height)
        {
            auto &d = owner->device;
            auto &gl = owner->gl;

            auto info = Vulkan::ImageCreateInfo::immutable_2d_image(width, height, VK_FORMAT_R8G8B8A8_UNORM, false);
            info.usage = ImageUsage;
            info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            info.misc = Vulkan::IMAGE_MISC_EXTERNAL_MEMORY_BIT;
            image = d.create_image(info);
            if (!image)
                fail("external image allocation failed");

            ready = d.request_semaphore_external(VK_SEMAPHORE_TYPE_BINARY, SemaphoreHandle);
            released = d.request_semaphore_external(VK_SEMAPHORE_TYPE_BINARY, SemaphoreHandle);
            if (!ready || !released)
                fail("external semaphore allocation failed");

            gl.CreateMemoryObjectsEXT(1, &memory);

            const int dedicated = 1;
            gl.MemoryObjectParameterivEXT(memory, 0x9581, &dedicated);
            gl.check("memory object parameters");
            gl.import(memory, image->export_handle(), image->get_allocation().get_size());

            int previous = 0;
            gl.GetIntegerv(0x8069, &previous);
            gl.GenTextures(1, &texture);
            gl.BindTexture(Texture2D, texture);
            gl.TexStorageMem2DEXT(Texture2D, 1, 0x8058, width, height, memory, image->get_allocation().get_offset());
            gl.TexParameteri(Texture2D, 0x2801, 0x2601);
            gl.TexParameteri(Texture2D, 0x2800, 0x2601);
            gl.TexParameteri(Texture2D, 0x2802, 0x812F);
            gl.TexParameteri(Texture2D, 0x2803, 0x812F);
            gl.BindTexture(Texture2D, previous);
            gl.check("shared texture storage");
            gl.GenSemaphoresEXT(1, &glReady);
            gl.GenSemaphoresEXT(1, &glReleased);
            gl.check("GL semaphore creation");
            gl.import(glReady, exportSemaphore(ready));
            gl.import(glReleased, exportSemaphore(released));
            gl.check("shared texture creation");
        }

        ~Slot() override
        {
            // Slots are retired only on the GL thread after both APIs have finished.
            auto &gl = owner->gl;
            if (texture)
                gl.DeleteTextures(1, &texture);
            if (memory)
                gl.DeleteMemoryObjectsEXT(1, &memory);
            if (glReady)
                gl.DeleteSemaphoresEXT(1, &glReady);
            if (glReleased)
                gl.DeleteSemaphoresEXT(1, &glReleased);
        }

        uint32_t AcquireTexture() override
        {
            auto &gl = owner->gl;
            if (pendingReady)
            {
                gl.WaitSemaphoreEXT(glReady, 0, nullptr, 1, &texture, &ShaderRead);
                pendingReady = false;
            }
            else if (pendingRelease)
            {
                // Repeated host frame: consume the previous GL signal before signalling again.
                gl.WaitSemaphoreEXT(glReleased, 0, nullptr, 1, &texture, &General);
                pendingRelease = false;
            }
            acquired = true;
            gl.check("frame acquire");
            return texture;
        }

        void ReleaseTexture() override
        {
            if (!acquired)
                fail("release without acquire");
            owner->gl.SignalSemaphoreEXT(glReleased, 0, nullptr, 1, &texture, &General);
            owner->gl.Flush();
            owner->gl.check("frame release");
            pendingRelease = true;
            acquired = false;
        }

        void copy(const Vulkan::Image &source)
        {
            auto &d = owner->device;
            if (acquired)
                fail("attempted to overwrite an acquired frame");

            if (pendingReady)
            {
                AcquireTexture();
                ReleaseTexture();
            }

            if (pendingRelease)
            {
                auto wait = d.request_semaphore(VK_SEMAPHORE_TYPE_BINARY, released->get_semaphore(), false);
                wait->signal_external();
                wait->set_signal_is_foreign_queue();
                d.add_wait_semaphore(Vulkan::CommandBuffer::Type::Generic, std::move(wait), VK_PIPELINE_STAGE_2_TRANSFER_BIT, true);
                pendingRelease = false;
            }

            auto cmd = d.request_command_buffer();

            if (produced)
                cmd->acquire_image_barrier(*image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                           VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            else
                cmd->image_barrier(*image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

            cmd->copy_image(*image, source);
            cmd->release_image_barrier(*image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                       VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            d.submit(cmd);
            // Borrow the owned external binary semaphore for this one submission.
            auto signal = d.request_semaphore(VK_SEMAPHORE_TYPE_BINARY, ready->get_semaphore(), false);
            d.submit_empty(Vulkan::CommandBuffer::Type::Generic, nullptr, signal.get());
            signal->wait_external();
            produced = pendingReady = true;
        }
    };
}

class GSGLInterop::Impl
{
public:
    std::shared_ptr<DeviceState> state;
    std::array<std::shared_ptr<Slot>, 2> slots;
    unsigned next = 0;

    Impl()
    {
        if (!glfwGetCurrentContext())
            fail("a current OpenGL context is required");
        const char *required[] = {"GL_EXT_memory_object", "GL_EXT_semaphore",
#ifdef _WIN32
                                  "GL_EXT_memory_object_win32", "GL_EXT_semaphore_win32"
#else
                                  "GL_EXT_memory_object_fd", "GL_EXT_semaphore_fd"
#endif
        };
        for (auto name : required)
            if (!glfwExtensionSupported(name))
                fail(std::string("missing ") + name);
        state = std::make_shared<DeviceState>();
        // Validate actual GL import before boot, even if the game never scans out.
        for (auto &slot : slots)
        {
            slot = std::make_shared<Slot>(state);
            slot->initialize(1, 1);
        }
    }

    ~Impl()
    {
        state->gl.Finish();
        state->device.wait_idle();
    }
};

GSGLInterop::GSGLInterop() : m_impl(std::make_unique<Impl>())
{
}
GSGLInterop::~GSGLInterop() = default;

Vulkan::Device &GSGLInterop::device()
{
    return m_impl->state->device;
}

PresentationFrame GSGLInterop::present(const ParallelGS::ScanoutResult &scanout)
{
    if (!scanout.image)
        return {};

    if (scanout.image->get_format() != VK_FORMAT_R8G8B8A8_UNORM)
        fail("unexpected scanout format");

    const auto width = scanout.image->get_width(), height = scanout.image->get_height();
    auto &slot = m_impl->slots[m_impl->next++ % 2];
    if (slot->image->get_width() != width || slot->image->get_height() != height)
    {
        m_impl->state->gl.Finish();
        device().wait_idle();
        slot = std::make_shared<Slot>(m_impl->state);
        slot->initialize(width, height);
    }
    slot->copy(*scanout.image);
    PresentationFrame frame;
    frame.gpu = slot;
    frame.width = width;
    frame.height = height;
    frame.aspectRatio = 4.0f / 3.0f;
    if (scanout.mode_width && scanout.mode_height && scanout.internal_height)
        frame.aspectRatio *= (float(scanout.internal_width) / scanout.mode_width) /
                             (float(scanout.internal_height) / scanout.mode_height);
    return frame;
}
