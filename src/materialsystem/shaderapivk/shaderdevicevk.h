//

#ifndef SHADERDEVICEVK_H
#define SHADERDEVICEVK_H

#ifdef _WIN32
#pragma once
#endif

#include "indexbuffervk.h"
#include "meshvk.h"
#include "shaderapi/IShaderDevice.h"
#include "shaderdevicemgrvk.h"
#include "vertexbuffervk.h"

struct MyVkAdapterInfo;

class CShaderDeviceVk : public IShaderDevice
{
    ImageFormat _backBufferFormat;
    int _backBufferSize[2];

    struct MeshOffset
    {
        int vertexCount;
        int indexCount;
        int firstVertex;
        int firstIndex;
        VkPrimitiveTopology topology;
        UniformBufferObject ubo;
    };

  public:
    CShaderDeviceVk();
    ~CShaderDeviceVk();

    VkExtent2D CShaderDeviceVk::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities);
    void InitDevice(MyVkAdapterInfo &adapterInfo, const ShaderDeviceInfo_t &creationInfo);
    void CleanupSwapchain();
    void RecreateSwapchain();
    void CreateSwapchain();
    void CreateImageViews();
    void CreateRenderPass();
    void CreateDescriptorSetlayout();
    void CreateDescriptorSets();
    void CreateDescriptorPool();
    void CreateGraphicsPipeline();
    VkShaderModule CreateShaderModule(const uint32_t *code, const size_t size);
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateUniformBuffers();
    void CreateCommandBuffers();
    void CreateSyncObjects();

    void GetMatrices(VMatrix *view, VMatrix *proj, VMatrix *model);
    UniformBufferObject GetUniformBufferObject();
    void UpdateUniformBuffer(uint32_t currentImage);

    // Update command buffer with all meshes that want to be drawn
    void UpdateCommandBuffer(uint32_t currentImage);

    // Releases/reloads resources when other apps want some memory
    void ReleaseResources() override;
    void ReacquireResources() override;

    // returns the backbuffer format and dimensions
    ImageFormat GetBackBufferFormat() const override;
    void GetBackBufferDimensions(int &width, int &height) const override;

    // Returns the current adapter in use
    int GetCurrentAdapter() const override;

    // Are we using graphics?
    bool IsUsingGraphics() const override;

    // Use this to spew information about the 3D layer
    void SpewDriverInfo() const override;

    // What's the bit depth of the stencil buffer?
    int StencilBufferBits() const override;

    // Are we using a mode that uses MSAA
    bool IsAAEnabled() const override;

    // Does a page flip
    void Present() override;

    // Returns the window size
    void GetWindowSize(int &nWidth, int &nHeight) const override;

    // Gamma ramp control
    void SetHardwareGammaRamp(float fGamma, float fGammaTVRangeMin, float fGammaTVRangeMax, float fGammaTVExponent,
                              bool bTVEnabled) override;

    // Creates/ destroys a child window
    bool AddView(void *hWnd) override;
    void RemoveView(void *hWnd) override;

    // Activates a view
    void SetView(void *hWnd) override;

    // Shader compilation
    IShaderBuffer *CompileShader(const char *pProgram, size_t nBufLen, const char *pShaderVersion) override;

    // Shader creation, destruction
    VertexShaderHandle_t CreateVertexShader(IShaderBuffer *pShaderBuffer) override;
    void DestroyVertexShader(VertexShaderHandle_t hShader) override;
    GeometryShaderHandle_t CreateGeometryShader(IShaderBuffer *pShaderBuffer) override;
    void DestroyGeometryShader(GeometryShaderHandle_t hShader) override;
    PixelShaderHandle_t CreatePixelShader(IShaderBuffer *pShaderBuffer) override;
    void DestroyPixelShader(PixelShaderHandle_t hShader) override;

    // NOTE: Deprecated!! Use CreateVertexBuffer/CreateIndexBuffer instead
    // Creates/destroys Mesh
    IMesh *CreateStaticMesh(VertexFormat_t vertexFormat, const char *pTextureBudgetGroup, IMaterial *pMaterial = NULL) override;
    void DestroyStaticMesh(IMesh *pMesh) override;

    // Creates/destroys static vertex + index buffers
    IVertexBuffer *CreateVertexBuffer(ShaderBufferType_t type, VertexFormat_t fmt, int nVertexCount, const char *pBudgetGroup) override;
    IVertexBuffer *CreateVertexBuffer(ShaderBufferType_t type, VertexFormat_t fmt, int nVertexCount, const char *pBudgetGroup,
                                      bool destination);
    void DestroyVertexBuffer(IVertexBuffer *pVertexBuffer) override;

    IIndexBuffer *CreateIndexBuffer(ShaderBufferType_t type, MaterialIndexFormat_t fmt, int nIndexCount, const char *pBudgetGroup) override;
    IIndexBuffer *CreateIndexBuffer(ShaderBufferType_t type, MaterialIndexFormat_t fmt, int nIndexCount, const char *pBudgetGroup,
                                    bool destination);
    void DestroyIndexBuffer(IIndexBuffer *pIndexBuffer) override;

    // Do we need to specify the stream here in the case of locking multiple dynamic VBs on different streams?
    IVertexBuffer *GetDynamicVertexBuffer(int nStreamID, VertexFormat_t vertexFormat, bool bBuffered = true) override;
    IIndexBuffer *GetDynamicIndexBuffer(MaterialIndexFormat_t fmt, bool bBuffered = true) override;

    // A special path used to tick the front buffer while loading on the 360
    void EnableNonInteractiveMode(MaterialNonInteractiveMode_t mode, ShaderNonInteractiveInfo_t *pInfo = NULL) override;
    void RefreshFrontBufferNonInteractive() override;
    void HandleThreadEvent(uint32 threadEvent) override;

    char *GetDisplayDeviceName() override;

    void ShutdownDevice();
    bool IsDeactivated() const;

    VkDevice GetDevice();

    void DrawMesh(CBaseMeshVk *pMesh);

    void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize srcOffset, VkDeviceSize dstOffset, VkDeviceSize size);

    void SetClearColor(VkClearValue color) { m_ClearColor = color; }

    void *GetCurrentViewHandle() { return m_ViewHWnd; }

    bool IsResizing() { return m_bIsResizing; }

  private:
    VkDevice m_Device;
    VkQueue m_GraphicsQueue;
    VkQueue m_PresentQueue;

    VkSwapchainKHR m_Swapchain;
    std::vector<VkImage> m_SwapchainImages;
    VkFormat m_SwapchainImageFormat;
    std::vector<VkImageView> m_SwapchainImageViews;
    VkExtent2D m_SwapchainExtent;

    VkShaderModule m_VertShaderModule;
    VkShaderModule m_FragShaderModule;

    VkRenderPass m_RenderPass;
    VkDescriptorSetLayout m_DescriptorSetLayout;
    VkDescriptorPool m_DescriptorPool;
    std::vector<VkDescriptorSet> m_DescriptorSets;
    VkPipelineLayout m_PipelineLayout;
    VkPipeline m_GraphicsPipeline;

    std::vector<VkFramebuffer> m_SwapchainFramebuffers;
    std::vector<VkBuffer> m_UniformBuffers;
    std::vector<VkDeviceMemory> m_UniformBuffersMemory;

    VkCommandPool m_CommandPool;
    std::vector<VkCommandBuffer> m_CommandBuffers;

    std::vector<VkSemaphore> m_ImageAvailableSemaphores;
    std::vector<VkSemaphore> m_RenderFinishedSemaphores;
    std::vector<VkFence> m_InFlightFences;
    size_t m_iCurrentFrame = 0;

    CVertexBufferVk *m_pVertexBuffer;
    CIndexBufferVk *m_pIndexBuffer;
    VkDeviceSize m_VertexBufferOffset;
    VkDeviceSize m_IndexBufferOffset;

    VkClearValue m_ClearColor = {0.0f, 0.0f, 0.0f, 1.0f};

    std::vector<MeshOffset> m_DrawMeshes;
    size_t m_DynamicUBOAlignment;

    // VK_TODO: detect window resize and modify this
    bool m_bFrameBufferResized = false;

    bool m_bIsMinimized : 1;
    // The current view hwnd
    void *m_ViewHWnd;
    int m_nWindowWidth;
    int m_nWindowHeight;

    bool m_bIsResizing;
};

extern CShaderDeviceVk *g_pShaderDevice;

// used to determine if we're deactivated
FORCEINLINE bool CShaderDeviceVk::IsDeactivated() const
{
    // VK_TODO
    // return ( IsPC() && ( ( m_DeviceState != DEVICE_STATE_OK ) || m_bQueuedDeviceLost ||
    // m_numReleaseResourcesRefCount) );
    return false;
}

//-----------------------------------------------------------------------------
// Helper class to reduce code related to shader buffers
//-----------------------------------------------------------------------------
template <class T> class CShaderBuffer : public IShaderBuffer
{
  public:
    CShaderBuffer(T *pBlob) : m_pBlob(pBlob) {}

    virtual size_t GetSize() const { return m_pBlob ? m_pBlob->size() : 0; }

    virtual const void *GetBits() const { return m_pBlob ? m_pBlob->data() : NULL; }

    virtual void Release()
    {
        if (m_pBlob)
        {
            m_pBlob->clear();
            delete m_pBlob;
        }
        delete this;
    }

  private:
    T *m_pBlob;
};

#endif // SHADERDEVICEVK_H