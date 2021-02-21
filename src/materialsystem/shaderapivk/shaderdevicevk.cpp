#include "shaderdevicevk.h"
#include <chrono>
#include <fstream>
#include "buffervkutil.h"
#include "shaderapivk.h"
#include "shadermanagervk.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
static CShaderDeviceVk g_ShaderDeviceVk;
CShaderDeviceVk *g_pShaderDevice = &g_ShaderDeviceVk;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CShaderDeviceVk, IShaderDevice, SHADER_DEVICE_INTERFACE_VERSION, g_ShaderDeviceVk)

const int MAX_FRAMES_IN_FLIGHT = 2;
const uint64_t MAX_MESHES = 1024; // VK_TODO: find a way to remove these
const uint64_t MAX_VERTICES = 65535 * 8;
const uint64_t MAX_INDICES = 65535 * 8;

const std::vector<const char *> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME};

struct SwapchainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// Get details about swapchain support
SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device)
{
    SwapchainSupportDetails details{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, g_pShaderDeviceMgr->GetSurface(), &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, g_pShaderDeviceMgr->GetSurface(), &formatCount, nullptr);
    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, g_pShaderDeviceMgr->GetSurface(), &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, g_pShaderDeviceMgr->GetSurface(), &presentModeCount, nullptr);
    if (presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, g_pShaderDeviceMgr->GetSurface(), &presentModeCount, details.presentModes.data());
    }

    return details;
}

// Choose the present mode for swapchain
VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes)
{
    // VK_TODO:
    // Check for vsync settings
    // VK_PRESENT_MODE_IMMEDIATE_KHR	= no vsync
    // VK_PRESENT_MODE_FIFO_KHR			= vsync
    // VK_PRESENT_MODE_FIFO_RELAXED_KHR	= vsync unless frame is late
    // VK_PRESENT_MODE_MAILBOX_KHR		= triple buffering

    for (const auto &availablePresentMode : availablePresentModes)
    {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return availablePresentMode;
        }
    }

    // VK_PRESENT_MODE_FIFO_KHR should be available in all devices
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D CShaderDeviceVk::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities)
{
    GetWindowSize(m_nWindowWidth, m_nWindowHeight);
    VkExtent2D actualExtent = {m_nWindowWidth, m_nWindowHeight};

    actualExtent.width = std::max(actualExtent.width, capabilities.minImageExtent.width);
    actualExtent.width = std::min(actualExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::max(actualExtent.height, capabilities.minImageExtent.height);
    actualExtent.height = std::min(actualExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats)
{
    for (const auto &availableFormat : availableFormats)
    {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

CShaderDeviceVk::CShaderDeviceVk()
{
    m_Device = VK_NULL_HANDLE;
    m_GraphicsQueue = VK_NULL_HANDLE;
    m_PresentQueue = VK_NULL_HANDLE;
}

CShaderDeviceVk::~CShaderDeviceVk() {}

void CShaderDeviceVk::InitDevice(MyVkAdapterInfo &adapterInfo, const ShaderDeviceInfo_t &creationInfo)
{
    uint32_t queueFamily;
    GetQueueFamily(adapterInfo.device, queueFamily);

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(adapterInfo.device, &features);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(adapterInfo.device, &properties);

    // Get list of supported extensions
#ifdef DEBUG
    Msg("Supported device extensions:\n");
#endif
    std::vector<char *> supportedExtensions;
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(adapterInfo.device, nullptr, &extCount, nullptr);
    if (extCount > 0)
    {
        std::vector<VkExtensionProperties> extensions(extCount);
        if (vkEnumerateDeviceExtensionProperties(adapterInfo.device, nullptr, &extCount, &extensions.front()) == VK_SUCCESS)
        {
            for (auto ext : extensions)
            {
#ifdef DEBUG
                Msg("  %s\n", ext.extensionName);
#endif
                supportedExtensions.push_back(ext.extensionName);
            }
        }
    }
    // VK_TODO: check if device extension are supported

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledLayerCount = validationLayers.size();
    createInfo.ppEnabledLayerNames = validationLayers.data();
    createInfo.enabledExtensionCount = deviceExtensions.size();
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.pEnabledFeatures = &features;

    // Enable dynamic features
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamicFeatures{};
    dynamicFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    dynamicFeatures.extendedDynamicState = true;
    createInfo.pNext = &dynamicFeatures;

    size_t minUboAlignment = properties.limits.minUniformBufferOffsetAlignment;
    m_DynamicUBOAlignment = sizeof(UniformBufferObject);
    if (minUboAlignment > 0)
    {
        m_DynamicUBOAlignment = (m_DynamicUBOAlignment + minUboAlignment - 1) & ~(minUboAlignment - 1);
    }

    // Create logical device
    vkCheck(vkCreateDevice(adapterInfo.device, &createInfo, g_pAllocCallbacks, &m_Device), "failed to create device");

    // Get queue
    vkGetDeviceQueue(m_Device, queueFamily, 0, &m_GraphicsQueue);
    vkGetDeviceQueue(m_Device, queueFamily, 0, &m_PresentQueue);

    CreateSwapchain();
    CreateImageViews();
    CreateRenderPass();
    CreateDescriptorSetlayout();
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateCommandPool();
    m_pVertexBuffer =
        (CVertexBufferVk *)CreateVertexBuffer(SHADER_BUFFER_TYPE_STATIC, VERTEX_FORMAT_UNKNOWN, MAX_VERTICES, "CVertexBufferVk", true);
    m_pIndexBuffer =
        (CIndexBufferVk *)CreateIndexBuffer(SHADER_BUFFER_TYPE_STATIC, MATERIAL_INDEX_FORMAT_16BIT, MAX_INDICES, "CIndexBufferVk", true);
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateCommandBuffers();
    CreateSyncObjects();

    g_pHardwareConfig->SetupHardwareCaps(creationInfo, g_pShaderDeviceMgr->GetHardwareCaps(adapterInfo));

    g_pShaderAPI->OnDeviceInit();
}

void CShaderDeviceVk::CreateSwapchain()
{
    VkPhysicalDevice adapter = g_pShaderDeviceMgr->GetAdapter();

    // Get details for swapchain
    SwapchainSupportDetails swapchainSupport = QuerySwapchainSupport(adapter);
    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapchainSupport.formats);
    VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapchainSupport.presentModes);
    VkExtent2D extent = ChooseSwapExtent(swapchainSupport.capabilities);

    // Maximum number of images in the swapchain
    uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;
    if (swapchainSupport.capabilities.maxImageCount > 0 && imageCount > swapchainSupport.capabilities.maxImageCount)
    {
        imageCount = swapchainSupport.capabilities.maxImageCount;
    }

    // Swapchain settings
    VkSwapchainCreateInfoKHR swapchainCreateInfo{};
    swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface = g_pShaderDeviceMgr->GetSurface();
    swapchainCreateInfo.imageFormat = surfaceFormat.format;
    swapchainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainCreateInfo.imageExtent = extent;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.minImageCount = swapchainSupport.capabilities.minImageCount;
    // VK_TODO: support for VK_SHARING_MODE_CONCURRENT and multiple queues?
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.queueFamilyIndexCount = 0;
    swapchainCreateInfo.pQueueFamilyIndices = nullptr;
    swapchainCreateInfo.preTransform = swapchainSupport.capabilities.currentTransform;
    swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCreateInfo.presentMode = presentMode;
    swapchainCreateInfo.clipped = VK_TRUE;
    swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

    // Check surface support
    VkBool32 supported;
    uint32_t queueFamily;
    GetQueueFamily(adapter, queueFamily);
    vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(adapter, queueFamily, swapchainCreateInfo.surface, &supported),
            "failed to check surface support");

    if (supported != VK_TRUE)
    {
        Error("surface not supported");
    }

    // Create swapchain
    vkCheck(vkCreateSwapchainKHR(m_Device, &swapchainCreateInfo, g_pAllocCallbacks, &m_Swapchain), "failed to create swapchain");

    // Get images
    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, nullptr);
    m_SwapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, m_SwapchainImages.data());

    // Store swapchain variables for later use
    m_SwapchainImageFormat = surfaceFormat.format;
    m_SwapchainExtent = extent;
}

void CShaderDeviceVk::RecreateSwapchain()
{
    // VK_TODO: do we need to handle minimization here?
    // I don't think IShaderDevice::Present gets called
    // at all when minimized

    vkDeviceWaitIdle(m_Device);

    CleanupSwapchain();

    // VK_TODO: this seems terribly inefficient.
    // At the moment this is needed if RecreateSwapChain() is called from SetView().
    g_pShaderDeviceMgr->DestroyVkSurface();
    g_pShaderDeviceMgr->CreateVkSurface(m_ViewHWnd);

    CreateSwapchain();
    CreateImageViews();
    CreateRenderPass();
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateCommandBuffers();
}

void CShaderDeviceVk::CreateImageViews()
{
    m_SwapchainImageViews.resize(m_SwapchainImages.size());

    for (size_t i = 0; i < m_SwapchainImages.size(); i++)
    {
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_SwapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_SwapchainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        vkCheck(vkCreateImageView(m_Device, &createInfo, g_pAllocCallbacks, &m_SwapchainImageViews[i]), "failed to create image view");
    }
}

void CShaderDeviceVk::CreateRenderPass()
{
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = m_SwapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    // Subpass dependecies
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    vkCheck(vkCreateRenderPass(m_Device, &renderPassInfo, g_pAllocCallbacks, &m_RenderPass), "failed to create render pass");
}

static std::vector<char> ReadFile(const std::string &filename)
{
    // TODO: replace with Source filesystem
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

void CShaderDeviceVk::CreateDescriptorSetlayout()
{
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    vkCheck(vkCreateDescriptorSetLayout(m_Device, &layoutInfo, g_pAllocCallbacks, &m_DescriptorSetLayout),
            "failed to create descriptor set layout");
}

void CShaderDeviceVk::CreateDescriptorSets()
{
    std::vector<VkDescriptorSetLayout> layouts(m_SwapchainImages.size(), m_DescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(m_SwapchainImages.size());
    allocInfo.pSetLayouts = layouts.data();

    m_DescriptorSets.resize(m_SwapchainImages.size());
    vkCheck(vkAllocateDescriptorSets(m_Device, &allocInfo, m_DescriptorSets.data()), "failed to allocate descriptor sets");

    for (size_t i = 0; i < m_SwapchainImages.size(); i++)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_UniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_DescriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        descriptorWrite.pImageInfo = nullptr;
        descriptorWrite.pTexelBufferView = nullptr;

        vkUpdateDescriptorSets(m_Device, 1, &descriptorWrite, 0, nullptr);
    }
}

void CShaderDeviceVk::CreateDescriptorPool()
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSize.descriptorCount = static_cast<uint32_t>(m_SwapchainImages.size());

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<uint32_t>(m_SwapchainImages.size());
    poolInfo.flags = 0;

    vkCheck(vkCreateDescriptorPool(m_Device, &poolInfo, g_pAllocCallbacks, &m_DescriptorPool), "failed to create descriptor pool");
}

void CShaderDeviceVk::CreateGraphicsPipeline()
{
    // Shader modules
    auto vertShaderCode = ReadFile("shaders/vert.spv");
    auto fragShaderCode = ReadFile("shaders/frag.spv");

    m_VertShaderModule = CreateShaderModule(reinterpret_cast<const uint32_t *>(vertShaderCode.data()), vertShaderCode.size());
    m_FragShaderModule = CreateShaderModule(reinterpret_cast<const uint32_t *>(fragShaderCode.data()), fragShaderCode.size());

    // Shader stages
    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = m_VertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = m_FragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input
    auto bindingDescription = Vertex::GetBindingDescription();
    auto attributeDescriptions = Vertex::GetAttributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissors
    VkViewport viewport = {};
    viewport.width = (float)m_SwapchainExtent.width;
    viewport.x = 0.0f;
    // Use bottom left as 0,0
    viewport.height = -(float)m_SwapchainExtent.height;
    viewport.y = (float)m_SwapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = m_SwapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = g_pShaderAPI->GetCullMode();
    rasterizer.frontFace = g_pShaderAPI->GetFrontFace();
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    // Depth and stencil testing
    VkPipelineDepthStencilStateCreateInfo *depthstencil = nullptr;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    // Dynamic states
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT};
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 1;
    dynamicState.pDynamicStates = dynamicStates;

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    vkCheck(vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, g_pAllocCallbacks, &m_PipelineLayout),
            "failed to create pipeline layout");

    // Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = depthstencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    // VK_FIXME: this causes validation layer errors when setting topology in UpdateCommandBuffer(),
    // the extension seems to work even without setting dynamic state?
    // pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = m_PipelineLayout;
    pipelineInfo.renderPass = m_RenderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    vkCheck(vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, g_pAllocCallbacks, &m_GraphicsPipeline),
            "failed to create graphics pipeline");

    // Cleanup
    vkDestroyShaderModule(m_Device, m_FragShaderModule, g_pAllocCallbacks);
    vkDestroyShaderModule(m_Device, m_VertShaderModule, g_pAllocCallbacks);
}

VkShaderModule CShaderDeviceVk::CreateShaderModule(const uint32_t *code, const size_t size)
{
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = size;
    createInfo.pCode = code;
    VkShaderModule shaderModule;
    vkCheck(vkCreateShaderModule(m_Device, &createInfo, g_pAllocCallbacks, &shaderModule), "failed to create shader module");
    return shaderModule;
}

void CShaderDeviceVk::CreateFramebuffers()
{
    m_SwapchainFramebuffers.resize(m_SwapchainImageViews.size());

    for (size_t i = 0; i < m_SwapchainImageViews.size(); i++)
    {
        VkImageView attachments[] = {m_SwapchainImageViews[i]};

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_SwapchainExtent.width;
        framebufferInfo.height = m_SwapchainExtent.height;
        framebufferInfo.layers = 1;

        vkCheck(vkCreateFramebuffer(m_Device, &framebufferInfo, g_pAllocCallbacks, &m_SwapchainFramebuffers[i]),
                "failed to create framebuffers");
    }
}

void CShaderDeviceVk::CreateCommandPool()
{
    uint32_t queueFamily;
    GetQueueFamily(g_pShaderDeviceMgr->GetAdapter(), queueFamily);

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    vkCheck(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool), "failed to create command pool");
}

void CShaderDeviceVk::CreateUniformBuffers()
{
    const VkDeviceSize bufferSize = MAX_MESHES * m_DynamicUBOAlignment;

    m_UniformBuffers.resize(m_SwapchainImages.size());
    m_UniformBuffersMemory.resize(m_SwapchainImages.size());

    for (size_t i = 0; i < m_SwapchainImages.size(); i++)
    {
        CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, m_UniformBuffers[i],
                     m_UniformBuffersMemory[i]);
    }
}

void CShaderDeviceVk::CreateCommandBuffers()
{
    m_CommandBuffers.resize(m_SwapchainFramebuffers.size());

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_CommandBuffers.size();

    vkCheck(vkAllocateCommandBuffers(m_Device, &allocInfo, m_CommandBuffers.data()), "failed to allocate command buffers");
}

void CShaderDeviceVk::CreateSyncObjects()
{
    m_ImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_RenderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_InFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkCheck(vkCreateSemaphore(m_Device, &semaphoreInfo, g_pAllocCallbacks, &m_ImageAvailableSemaphores[i]),
                "failed to create semaphore");
        vkCheck(vkCreateSemaphore(m_Device, &semaphoreInfo, g_pAllocCallbacks, &m_RenderFinishedSemaphores[i]),
                "failed to create semaphore");
        vkCheck(vkCreateFence(m_Device, &fenceInfo, g_pAllocCallbacks, &m_InFlightFences[i]), "failed to create fence");
    }
}

void CShaderDeviceVk::GetMatrices(VMatrix *view, VMatrix *proj, VMatrix *model)
{
    IMatRenderContext *renderContext = g_pMaterialSystem->GetRenderContext();
    renderContext->GetMatrix(MATERIAL_VIEW, view);
    renderContext->GetMatrix(MATERIAL_PROJECTION, proj);
    renderContext->GetMatrix(MATERIAL_MODEL, model);
}

UniformBufferObject CShaderDeviceVk::GetUniformBufferObject()
{
    VMatrix viewMatrix, projMatrix, modelMatrix;
    GetMatrices(&viewMatrix, &projMatrix, &modelMatrix);

    UniformBufferObject ubo = {};
    ubo.view = glm::make_mat4x4(viewMatrix.Base());
    ubo.proj = glm::make_mat4x4(projMatrix.Base());
    ubo.model = glm::make_mat4x4(modelMatrix.Base());

    // Source uses row-major storage,
    // we want column-major.
    ubo.view = glm::transpose(ubo.view);
    ubo.proj = glm::transpose(ubo.proj);
    ubo.model = glm::transpose(ubo.model);

    return ubo;
}

void CShaderDeviceVk::UpdateUniformBuffer(uint32_t currentImage)
{
    if (m_DrawMeshes.empty())
    {
        return;
    }

    const size_t bufferSize = m_DrawMeshes.size() * m_DynamicUBOAlignment;
    void *dynamicUBO = MemAlloc_AllocAligned(bufferSize, m_DynamicUBOAlignment);
    vkMapMemory(m_Device, m_UniformBuffersMemory[currentImage], 0, bufferSize, 0, &dynamicUBO);

    for (size_t i = 0; i < m_DrawMeshes.size(); i++)
    {
        UniformBufferObject *ubo = (UniformBufferObject *)((uint64_t)dynamicUBO + (i * m_DynamicUBOAlignment));
        memcpy(ubo, &m_DrawMeshes[i].ubo, sizeof(UniformBufferObject));
    }

    // Flush to make changes visible to the host
    VkMappedMemoryRange memoryRange{};
    memoryRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    memoryRange.memory = m_UniformBuffersMemory[currentImage];
    memoryRange.size = bufferSize;
    vkFlushMappedMemoryRanges(m_Device, 1, &memoryRange);

    vkUnmapMemory(m_Device, m_UniformBuffersMemory[currentImage]);
    MemAlloc_FreeAligned(dynamicUBO);
}

void CShaderDeviceVk::UpdateCommandBuffer(uint32_t currentImage)
{
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    vkCheck(vkBeginCommandBuffer(m_CommandBuffers[currentImage], &beginInfo), "failed to begin command buffer");

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = m_SwapchainFramebuffers[currentImage];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_SwapchainExtent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &m_ClearColor;

    vkCmdBeginRenderPass(m_CommandBuffers[currentImage], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(m_CommandBuffers[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

    if (!m_DrawMeshes.empty())
    {
        VkBuffer vertexBuffers[] = {*(m_pVertexBuffer->GetVkBuffer())};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(m_CommandBuffers[currentImage], 0, 1, vertexBuffers, offsets);

        vkCmdBindIndexBuffer(m_CommandBuffers[currentImage], *m_pIndexBuffer->GetVkBuffer(), 0, VK_INDEX_TYPE_UINT16);

        for (size_t i = 0; i < m_DrawMeshes.size(); i++)
        {
            // bind descriptor set for current mesh
            uint32_t dynamicOffset = i * m_DynamicUBOAlignment;
            vkCmdBindDescriptorSets(m_CommandBuffers[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1,
                                    &m_DescriptorSets[currentImage], 1, &dynamicOffset);

            // set topology
            VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdSetPrimitiveTopologyEXT(m_CommandBuffers[currentImage], m_DrawMeshes[i].topology);

            // draw
            vkCmdDrawIndexed(m_CommandBuffers[currentImage], m_DrawMeshes[i].indexCount, 1, m_DrawMeshes[i].firstIndex,
                             m_DrawMeshes[i].firstVertex, 0);
        }
    }

    vkCmdEndRenderPass(m_CommandBuffers[currentImage]);

    vkCheck(vkEndCommandBuffer(m_CommandBuffers[currentImage]), "failed to end command buffer");

    // Meshes that want to be drawn should call Draw every frame
    m_VertexBufferOffset = 0;
    m_IndexBufferOffset = 0;
    m_DrawMeshes.resize(0);
}

void CShaderDeviceVk::ReleaseResources() {}

void CShaderDeviceVk::ReacquireResources() {}

ImageFormat CShaderDeviceVk::GetBackBufferFormat() const { return _backBufferFormat; }

void CShaderDeviceVk::GetBackBufferDimensions(int &width, int &height) const
{
    width = _backBufferSize[0];
    height = _backBufferSize[1];
}

int CShaderDeviceVk::GetCurrentAdapter() const { return g_pShaderDeviceMgr->GetCurrentAdapter(); }

bool CShaderDeviceVk::IsUsingGraphics() const { return false; }

void CShaderDeviceVk::SpewDriverInfo() const { Warning("Vulkan\n"); }

int CShaderDeviceVk::StencilBufferBits() const { return 0; }

bool CShaderDeviceVk::IsAAEnabled() const { return false; }

void CShaderDeviceVk::Present()
{
    // Acquire image from swapchain
    uint32_t imageIndex;

    // Stop valve's min and max macro definitions from messing with numeric_limits::max()...
#pragma push_macro("max")
#undef max

    vkWaitForFences(m_Device, 1, &m_InFlightFences[m_iCurrentFrame], VK_TRUE, std::numeric_limits<uint64_t>::max());

    VkResult result = vkAcquireNextImageKHR(m_Device, m_Swapchain, std::numeric_limits<uint64_t>::max(),
                                            m_ImageAvailableSemaphores[m_iCurrentFrame], VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapchain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        Error("failed to acquire swapchain image!");
    }

#pragma pop_macro("max")

    UpdateUniformBuffer(imageIndex);

    UpdateCommandBuffer(imageIndex);

    // Submit commandbuffer
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_ImageAvailableSemaphores[m_iCurrentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_CommandBuffers[imageIndex];

    VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphores[m_iCurrentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkResetFences(m_Device, 1, &m_InFlightFences[m_iCurrentFrame]);

    vkCheck(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFences[m_iCurrentFrame]), "failed to submit queue");

    // Present
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapchains[] = {m_Swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr;

    result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_bFrameBufferResized)
    {
        m_bFrameBufferResized = false;
        RecreateSwapchain();
    }
    else if (result != VK_SUCCESS)
    {
        Error("failed to present swapchain image!");
    }

    m_iCurrentFrame = (m_iCurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void CShaderDeviceVk::GetWindowSize(int &nWidth, int &nHeight) const
{
    // If the window was minimized last time swap buffers happened, or if it's iconic now,
    // return 0 size
    if (!m_bIsMinimized && !IsIconic((HWND)g_pShaderDeviceMgr->GetWindowHandle()))
    {
        // NOTE: Use the 'current view' (which may be the same as the main window)
        Assert(m_ViewHWnd != nullptr);
        RECT rect;
        GetClientRect((HWND)m_ViewHWnd, &rect);
        nWidth = rect.right - rect.left;
        nHeight = rect.bottom - rect.top;
    }
    else
    {
        nWidth = nHeight = 0;
    }
}

void CShaderDeviceVk::SetHardwareGammaRamp(float fGamma, float fGammaTVRangeMin, float fGammaTVRangeMax, float fGammaTVExponent,
                                           bool bTVEnabled)
{
}

bool CShaderDeviceVk::AddView(void *hWnd)
{
    // If we haven't created a device yet
    if (!m_Device)
        return false;

    /*
    // Make sure no duplicate hwnds...
    if (FindView(hWnd) >= 0)
        return false;

    // In this case, we need to create the device; this is our
    // default swap chain. This here says we're gonna use a part of the
    // existing buffer and just grab that.
    int view = m_Views.AddToTail();
    m_Views[view].m_HWnd = (HWND)hWnd;

    //	memcpy( &m_Views[view].m_PresentParamters, m_PresentParameters, sizeof(m_PresentParamters) );
    HRESULT hr;
    hr = g_pShaderDevice->CreateAdditionalSwapChain( &m_PresentParameters, &m_Views[view].m_pSwapChain );
    return !FAILED(hr);
    */

    return true;
}

void CShaderDeviceVk::RemoveView(void *hWnd)
{
    /*
    // Look for the view in the list of views
    int i = FindView(hwnd);
    if (i >= 0)
    {
    // FIXME		m_Views[i].m_pSwapChain->Release();
    m_Views.FastRemove(i);
    }
    */
}

void CShaderDeviceVk::SetView(void *hWnd)
{
    if (m_Device && !m_DrawMeshes.empty())
    {
        // We have something to draw,
        // do so before switching views.
        Present();
    }

    ShaderViewport_t viewport;
    ShaderAPI()->GetViewports(&viewport, 1);

    // Get the window (*not* client) rect of the view window
    m_ViewHWnd = (HWND)hWnd;
    GetWindowSize(m_nWindowWidth, m_nWindowHeight);

    // Reset the viewport (takes into account the view rect)
    // Don't need to set the viewport if it's not ready
    ShaderAPI()->SetViewports(1, &viewport);

    // VK_TODO: This seems inefficient,
    // need better implementation for multiple views.
    if (m_Device)
        RecreateSwapchain();
}

IShaderBuffer *CShaderDeviceVk::CompileShader(const char *pProgram, size_t nBufLen, const char *pShaderVersion)
{
    return g_pShaderManager->CompileShader(pProgram, nBufLen, pShaderVersion);
}

VertexShaderHandle_t CShaderDeviceVk::CreateVertexShader(IShaderBuffer *pShaderBuffer)
{
    return g_pShaderManager->CreateVertexShader(pShaderBuffer);
}

void CShaderDeviceVk::DestroyVertexShader(VertexShaderHandle_t hShader) { g_pShaderManager->DestroyVertexShader(hShader); }

GeometryShaderHandle_t CShaderDeviceVk::CreateGeometryShader(IShaderBuffer *pShaderBuffer)
{
    Assert(0);
    return GEOMETRY_SHADER_HANDLE_INVALID;
}

void CShaderDeviceVk::DestroyGeometryShader(GeometryShaderHandle_t hShader) { Assert(hShader == GEOMETRY_SHADER_HANDLE_INVALID); }

PixelShaderHandle_t CShaderDeviceVk::CreatePixelShader(IShaderBuffer *pShaderBuffer)
{
    return g_pShaderManager->CreatePixelShader(pShaderBuffer);
}

void CShaderDeviceVk::DestroyPixelShader(PixelShaderHandle_t hShader) { g_pShaderManager->DestroyPixelShader(hShader); }

IMesh *CShaderDeviceVk::CreateStaticMesh(VertexFormat_t vertexFormat, const char *pTextureBudgetGroup, IMaterial *pMaterial)
{
    return g_pMeshMgr->CreateStaticMesh(vertexFormat, pTextureBudgetGroup, pMaterial);
}

void CShaderDeviceVk::DestroyStaticMesh(IMesh *pMesh) { g_pMeshMgr->DestroyStaticMesh(pMesh); }

IVertexBuffer *CShaderDeviceVk::CreateVertexBuffer(ShaderBufferType_t type, VertexFormat_t fmt, int nVertexCount, const char *pBudgetGroup)
{
    return CreateVertexBuffer(type, fmt, nVertexCount, pBudgetGroup, false);
}

IVertexBuffer *CShaderDeviceVk::CreateVertexBuffer(ShaderBufferType_t type, VertexFormat_t fmt, int nVertexCount, const char *pBudgetGroup,
                                                   bool destination)
{
    CVertexBufferVk *pVertexBufferVk = new CVertexBufferVk(type, fmt, nVertexCount, pBudgetGroup, destination);
    pVertexBufferVk->Allocate();
    return pVertexBufferVk;
}

void CShaderDeviceVk::DestroyVertexBuffer(IVertexBuffer *pVertexBuffer)
{
    if (pVertexBuffer)
    {
        delete pVertexBuffer;
    }
}

IIndexBuffer *CShaderDeviceVk::CreateIndexBuffer(ShaderBufferType_t type, MaterialIndexFormat_t fmt, int nIndexCount,
                                                 const char *pBudgetGroup)
{
    return CreateIndexBuffer(type, fmt, nIndexCount, pBudgetGroup, false);
}

IIndexBuffer *CShaderDeviceVk::CreateIndexBuffer(ShaderBufferType_t type, MaterialIndexFormat_t fmt, int nIndexCount,
                                                 const char *pBudgetGroup, bool destination)
{
    switch (type)
    {
    case SHADER_BUFFER_TYPE_STATIC:
    case SHADER_BUFFER_TYPE_DYNAMIC:
    {
        CIndexBufferVk *pIndexBufferVk = new CIndexBufferVk(type, fmt, nIndexCount, pBudgetGroup, destination);
        pIndexBufferVk->Allocate();
        return pIndexBufferVk;
    }
    case SHADER_BUFFER_TYPE_STATIC_TEMP:
    case SHADER_BUFFER_TYPE_DYNAMIC_TEMP:
        Assert(0);
        return NULL;
    default:
        Assert(0);
        return NULL;
    }
}

void CShaderDeviceVk::DestroyIndexBuffer(IIndexBuffer *pIndexBuffer)
{
    if (pIndexBuffer)
    {
        delete pIndexBuffer;
    }
}

IVertexBuffer *CShaderDeviceVk::GetDynamicVertexBuffer(int nStreamID, VertexFormat_t vertexFormat, bool bBuffered)
{
    return g_pMeshMgr->GetDynamicVertexBuffer(nStreamID, vertexFormat, bBuffered);
}

IIndexBuffer *CShaderDeviceVk::GetDynamicIndexBuffer(MaterialIndexFormat_t fmt, bool bBuffered)
{
    return g_pMeshMgr->GetDynamicIndexBuffer(fmt, bBuffered);
}

void CShaderDeviceVk::EnableNonInteractiveMode(MaterialNonInteractiveMode_t mode, ShaderNonInteractiveInfo_t *pInfo) {}

void CShaderDeviceVk::RefreshFrontBufferNonInteractive() {}

void CShaderDeviceVk::HandleThreadEvent(uint32 threadEvent) {}

char *CShaderDeviceVk::GetDisplayDeviceName() { return "(unspecified)"; }

void CShaderDeviceVk::CleanupSwapchain()
{
    for (size_t i = 0; i < m_SwapchainFramebuffers.size(); i++)
    {
        vkDestroyFramebuffer(m_Device, m_SwapchainFramebuffers[i], g_pAllocCallbacks);
    }

    for (size_t i = 0; i < m_SwapchainImages.size(); i++)
    {
        vkDestroyBuffer(m_Device, m_UniformBuffers[i], g_pAllocCallbacks);
        vkFreeMemory(m_Device, m_UniformBuffersMemory[i], g_pAllocCallbacks);
    }

    vkDestroyDescriptorPool(m_Device, m_DescriptorPool, g_pAllocCallbacks);

    vkFreeCommandBuffers(m_Device, m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());

    vkDestroyPipeline(m_Device, m_GraphicsPipeline, g_pAllocCallbacks);
    vkDestroyPipelineLayout(m_Device, m_PipelineLayout, g_pAllocCallbacks);
    vkDestroyRenderPass(m_Device, m_RenderPass, g_pAllocCallbacks);

    for (size_t i = 0; i < m_SwapchainImageViews.size(); i++)
    {
        vkDestroyImageView(m_Device, m_SwapchainImageViews[i], g_pAllocCallbacks);
    }

    vkDestroySwapchainKHR(m_Device, m_Swapchain, g_pAllocCallbacks);
}

void CShaderDeviceVk::ShutdownDevice()
{
    // InitDevice might not have been called if we crash on startup
    if (!m_Device)
    {
        return;
    }

    g_pShaderAPI->OnDeviceShutdown();

    vkDeviceWaitIdle(m_Device);

    CleanupSwapchain();

    delete m_pVertexBuffer;
    delete m_pIndexBuffer;

    vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(m_Device, m_RenderFinishedSemaphores[i], g_pAllocCallbacks);
        vkDestroySemaphore(m_Device, m_ImageAvailableSemaphores[i], g_pAllocCallbacks);
        vkDestroyFence(m_Device, m_InFlightFences[i], g_pAllocCallbacks);
    }

    vkDestroyCommandPool(m_Device, m_CommandPool, g_pAllocCallbacks);

    vkDestroyDevice(m_Device, g_pAllocCallbacks);

    m_Device = nullptr;
}

VkDevice CShaderDeviceVk::GetDevice() { return m_Device; }

void CShaderDeviceVk::DrawMesh(CBaseMeshVk *pMesh)
{
    CVertexBufferVk *vertexBuffer = pMesh->GetVertexBuffer();
    CIndexBufferVk *indexBuffer = pMesh->GetIndexBuffer();

    int vertexCount = pMesh->VertexCount();
    int indexCount = pMesh->IndexCount();
    VkDeviceSize vertexRegionSize = vertexCount * vertexBuffer->VertexSize();
    VkDeviceSize indexRegionSize = indexCount * indexBuffer->IndexSize();

    // Copy mesh buffers to optimal destination buffers
    CopyBuffer(*vertexBuffer->GetVkBuffer(), *m_pVertexBuffer->GetVkBuffer(), 0, m_VertexBufferOffset, vertexRegionSize);
    CopyBuffer(*indexBuffer->GetVkBuffer(), *m_pIndexBuffer->GetVkBuffer(), 0, m_IndexBufferOffset, indexRegionSize);

    // Store current byte offsets for copying into buffers
    m_VertexBufferOffset += vertexRegionSize;
    m_IndexBufferOffset += indexRegionSize;

    // Get vertex and index offsets for this mesh
    int firstVertex = 0;
    int firstIndex = 0;
    if (!m_DrawMeshes.empty())
    {
        auto back = m_DrawMeshes.back();
        firstVertex += back.firstVertex + back.vertexCount;
        firstIndex += back.firstIndex + back.indexCount;
    }

    MeshOffset m;
    m.vertexCount = vertexCount;
    m.indexCount = indexCount;
    m.firstVertex = firstVertex;
    m.firstIndex = firstIndex;
    m.topology = ComputeMode(pMesh->GetPrimitiveType());
    m.ubo = GetUniformBufferObject();

    m_DrawMeshes.push_back(m);
}

void CShaderDeviceVk::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize srcOffset, VkDeviceSize dstOffset, VkDeviceSize size)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    Assert(copyRegion.size > 0);
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_GraphicsQueue);
    vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
}