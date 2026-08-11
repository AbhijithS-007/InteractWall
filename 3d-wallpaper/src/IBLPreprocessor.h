#pragma once
#include <d3d11.h>
#include <string>

// IBL Preprocessor — generates irradiance and prefiltered specular cubemaps
// from an equirectangular HDR image. Results are cached to disk as binary files.
class IBLPreprocessor {
public:
    // Initialize and generate IBL maps from the given HDR file.
    // cachePath: directory to store/load cached cubemaps (e.g. "envmaps/cache/")
    // Returns true on success.
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
                    const std::string& hdrPath, const std::string& cachePath,
                    const std::string& shaderDir);

    // Generate a procedural studio HDR environment (fallback when no .hdr file exists)
    bool InitializeProcedural(ID3D11Device* device, ID3D11DeviceContext* context,
                              const std::string& cachePath, const std::string& shaderDir);

    ID3D11ShaderResourceView* GetIrradianceSRV() const { return m_irradianceSRV; }
    ID3D11ShaderResourceView* GetPrefilteredSRV() const { return m_prefilteredSRV; }
    int GetMipCount() const { return m_prefilteredMipCount; }

    void Release();

private:
    static const int IRRADIANCE_SIZE = 32;
    static const int PREFILTERED_SIZE = 128;

    ID3D11ShaderResourceView* m_irradianceSRV = nullptr;
    ID3D11ShaderResourceView* m_prefilteredSRV = nullptr;
    int m_prefilteredMipCount = 0;

    // Internal helpers
    bool LoadHDR(const std::string& path, float** outData, int* outWidth, int* outHeight);
    bool GenerateProceduralHDR(float** outData, int* outWidth, int* outHeight);

    ID3D11ShaderResourceView* CreateEquirectTexture(ID3D11Device* device,
        ID3D11DeviceContext* context, float* data, int width, int height);

    bool ConvertEquirectToCubemap(ID3D11Device* device, ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* equirectSRV, ID3D11ShaderResourceView** outCubeSRV,
        ID3D11Texture2D** outCubeTex, int faceSize, const std::string& shaderDir);

    bool GenerateIrradianceMap(ID3D11Device* device, ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* cubeSRV, const std::string& shaderDir);

    bool GeneratePrefilterMap(ID3D11Device* device, ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* cubeSRV, const std::string& shaderDir);

    bool SaveCubemapToCache(ID3D11Device* device, ID3D11DeviceContext* context,
        ID3D11Texture2D* tex, const std::string& path);
    bool LoadCubemapFromCache(ID3D11Device* device, const std::string& path,
        ID3D11ShaderResourceView** outSRV, bool generateMips, int* outMipCount);

    ID3D11ComputeShader* LoadComputeShader(ID3D11Device* device, const std::string& csoPath);
};
