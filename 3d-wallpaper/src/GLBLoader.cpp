#define NOMINMAX
#define CGLTF_IMPLEMENTATION
#include "GLBLoader.h"
#include "cgltf.h"
#include <windows.h>
#include <wincodec.h>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cfloat>

#pragma comment(lib, "windowscodecs.lib")

// ---------------------------------------------------------------
// Helpers to read accessor data from cgltf
// ---------------------------------------------------------------
static const uint8_t* GetBufferData(const cgltf_accessor* accessor) {
    cgltf_buffer_view* view = accessor->buffer_view;
    return (const uint8_t*)view->buffer->data + view->offset + accessor->offset;
}

#include "TextureLoader.h"

// ---------------------------------------------------------------
// Create a texture SRV from embedded image data
// ---------------------------------------------------------------
static ID3D11ShaderResourceView* CreateTextureFromMemory(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const uint8_t* data,
    size_t dataSize,
    bool isSRGB = true)
{
    return TextureLoader::CreateTextureFromMemory(device, context, data, dataSize, isSRGB);
}

// ---------------------------------------------------------------
// Main loader
// ---------------------------------------------------------------
bool LoadGLB(ID3D11Device* device, ID3D11DeviceContext* context, const std::string& filepath, LoadedModel& outModel) {
    cgltf_options options = {};
    cgltf_data* data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);
    if (result != cgltf_result_success) {
        std::cout << "[GLBLoader] Failed to parse: " << filepath << "\n";
        return false;
    }

    result = cgltf_load_buffers(&options, data, filepath.c_str());
    if (result != cgltf_result_success) {
        std::cout << "[GLBLoader] Failed to load buffers: " << filepath << "\n";
        cgltf_free(data);
        return false;
    }

    std::cout << "[GLBLoader] Loaded: " << filepath
              << " (" << data->meshes_count << " meshes, "
              << data->images_count << " images)\n";

    // Initialize bounds
    outModel.boundsMin = { FLT_MAX, FLT_MAX, FLT_MAX };
    outModel.boundsMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

    // Iterate all nodes to respect glTF node hierarchy (fixes disconnected meshes)
    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;

        cgltf_mesh* mesh = node->mesh;
        
        // Compute world matrix for this node
        cgltf_float matrix[16];
        cgltf_node_transform_world(node, matrix);
        XMMATRIX nodeWorld = XMMATRIX(matrix);
        
        // Compute inverse transpose for normals
        XMVECTOR det;
        XMMATRIX nodeWorldInvTrans = XMMatrixTranspose(XMMatrixInverse(&det, nodeWorld));

        for (size_t pi = 0; pi < mesh->primitives_count; pi++) {
            cgltf_primitive& prim = mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            // Find position, normal, texcoord accessors
            const cgltf_accessor* posAccessor = nullptr;
            const cgltf_accessor* normAccessor = nullptr;
            const cgltf_accessor* uvAccessor = nullptr;
            const cgltf_accessor* uv1Accessor = nullptr;
            const cgltf_accessor* tangentAccessor = nullptr;

            for (size_t ai = 0; ai < prim.attributes_count; ai++) {
                if (prim.attributes[ai].type == cgltf_attribute_type_position)
                    posAccessor = prim.attributes[ai].data;
                else if (prim.attributes[ai].type == cgltf_attribute_type_normal)
                    normAccessor = prim.attributes[ai].data;
                else if (prim.attributes[ai].type == cgltf_attribute_type_texcoord && prim.attributes[ai].index == 0)
                    uvAccessor = prim.attributes[ai].data;
                else if (prim.attributes[ai].type == cgltf_attribute_type_texcoord && prim.attributes[ai].index == 1)
                    uv1Accessor = prim.attributes[ai].data;
                else if (prim.attributes[ai].type == cgltf_attribute_type_tangent)
                    tangentAccessor = prim.attributes[ai].data;
            }

            if (!posAccessor) continue;

            size_t vertexCount = posAccessor->count;
            // Build vertex array
            std::vector<MeshVertex> vertices(vertexCount);
            for (size_t vi = 0; vi < vertexCount; vi++) {
                float v[3];
                cgltf_accessor_read_float(posAccessor, vi, v, 3);
                
                // Transform position
                XMVECTOR pos = XMVectorSet(v[0], v[1], v[2], 1.0f);
                pos = XMVector3Transform(pos, nodeWorld);
                XMStoreFloat3(&vertices[vi].position, pos);
                
                if (normAccessor) {
                    cgltf_accessor_read_float(normAccessor, vi, v, 3);
                    // Transform normal
                    XMVECTOR norm = XMVectorSet(v[0], v[1], v[2], 0.0f);
                    norm = XMVector3Normalize(XMVector3Transform(norm, nodeWorldInvTrans));
                    XMStoreFloat3(&vertices[vi].normal, norm);
                } else {
                    vertices[vi].normal = { 0.0f, 1.0f, 0.0f };
                }

                if (uvAccessor) {
                    cgltf_accessor_read_float(uvAccessor, vi, v, 2);
                    vertices[vi].texcoord = { v[0], v[1] };
                } else {
                    vertices[vi].texcoord = { 0.0f, 0.0f };
                }
                
                if (uv1Accessor) {
                    cgltf_accessor_read_float(uv1Accessor, vi, v, 2);
                    vertices[vi].texcoord1 = { v[0], v[1] };
                } else {
                    vertices[vi].texcoord1 = vertices[vi].texcoord;
                }
                
                if (tangentAccessor) {
                    float tv[4];
                    cgltf_accessor_read_float(tangentAccessor, vi, tv, 4);
                    // Transform tangent
                    XMVECTOR tan = XMVectorSet(tv[0], tv[1], tv[2], 0.0f);
                    tan = XMVector3Normalize(XMVector3Transform(tan, nodeWorld));
                    XMStoreFloat3((XMFLOAT3*)&vertices[vi].tangent, tan);
                    vertices[vi].tangent.w = tv[3]; // Keep handedness
                } else {
                    vertices[vi].tangent = { 1.0f, 0.0f, 0.0f, 1.0f }; // Default tangent
                }

                // Update bounds
                outModel.boundsMin.x = std::min(outModel.boundsMin.x, vertices[vi].position.x);
                outModel.boundsMin.y = std::min(outModel.boundsMin.y, vertices[vi].position.y);
                outModel.boundsMin.z = std::min(outModel.boundsMin.z, vertices[vi].position.z);
                outModel.boundsMax.x = std::max(outModel.boundsMax.x, vertices[vi].position.x);
                outModel.boundsMax.y = std::max(outModel.boundsMax.y, vertices[vi].position.y);
                outModel.boundsMax.z = std::max(outModel.boundsMax.z, vertices[vi].position.z);
            }

            SubMesh submesh;
            submesh.vertexCount = (UINT)vertexCount;

            // Create vertex buffer
            D3D11_BUFFER_DESC vbDesc = {};
            vbDesc.ByteWidth = (UINT)(vertices.size() * sizeof(MeshVertex));
            vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
            vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

            D3D11_SUBRESOURCE_DATA vbData = {};
            vbData.pSysMem = vertices.data();

            HRESULT hr = device->CreateBuffer(&vbDesc, &vbData, &submesh.vertexBuffer);
            if (FAILED(hr)) {
                std::cout << "[GLBLoader] Failed to create vertex buffer\n";
                continue;
            }

            // Create index buffer if indices exist
            if (prim.indices) {
                const cgltf_accessor* idxAccessor = prim.indices;
                submesh.indexCount = (UINT)idxAccessor->count;

                const uint8_t* idxData = GetBufferData(idxAccessor);
                UINT idxByteSize;

                if (idxAccessor->component_type == cgltf_component_type_r_16u) {
                    submesh.indexFormat = DXGI_FORMAT_R16_UINT;
                    idxByteSize = submesh.indexCount * 2;
                } else {
                    // Convert to 32-bit indices
                    submesh.indexFormat = DXGI_FORMAT_R32_UINT;
                    idxByteSize = submesh.indexCount * 4;

                    if (idxAccessor->component_type == cgltf_component_type_r_8u) {
                        std::vector<uint32_t> idx32(submesh.indexCount);
                        for (size_t i = 0; i < submesh.indexCount; i++)
                            idx32[i] = idxData[i];
                        
                        D3D11_BUFFER_DESC ibDesc = {};
                        ibDesc.ByteWidth = idxByteSize;
                        ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
                        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
                        D3D11_SUBRESOURCE_DATA ibData = {};
                        ibData.pSysMem = idx32.data();
                        device->CreateBuffer(&ibDesc, &ibData, &submesh.indexBuffer);
                        
                        goto skipDefaultIndex;
                    }
                }

                {
                    D3D11_BUFFER_DESC ibDesc = {};
                    ibDesc.ByteWidth = idxByteSize;
                    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
                    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
                    D3D11_SUBRESOURCE_DATA ibData = {};
                    ibData.pSysMem = idxData;
                    device->CreateBuffer(&ibDesc, &ibData, &submesh.indexBuffer);
                }
                skipDefaultIndex:;
            }

            // Material: base color + texture + PBR
            if (prim.material) {
                cgltf_material& mat = *prim.material;
                if (mat.has_pbr_metallic_roughness) {
                    auto& pbr = mat.pbr_metallic_roughness;
                    submesh.baseColor = {
                        pbr.base_color_factor[0],
                        pbr.base_color_factor[1],
                        pbr.base_color_factor[2],
                        pbr.base_color_factor[3]
                    };
                    submesh.metallicFactor = pbr.metallic_factor;
                    submesh.roughnessFactor = pbr.roughness_factor;
                    
                    submesh.emissiveFactor = {
                        mat.emissive_factor[0],
                        mat.emissive_factor[1],
                        mat.emissive_factor[2]
                    };
                    
                    if (mat.has_emissive_strength) {
                        submesh.emissiveFactor.x *= mat.emissive_strength.emissive_strength;
                        submesh.emissiveFactor.y *= mat.emissive_strength.emissive_strength;
                        submesh.emissiveFactor.z *= mat.emissive_strength.emissive_strength;
                    }
                    
                    if (mat.alpha_mode == cgltf_alpha_mode_opaque) submesh.alphaMode = 0;
                    else if (mat.alpha_mode == cgltf_alpha_mode_mask) submesh.alphaMode = 1;
                    else if (mat.alpha_mode == cgltf_alpha_mode_blend) submesh.alphaMode = 2;
                    
                    if (mat.has_transmission) {
                        submesh.alphaMode = 2;
                        submesh.baseColor.w = std::min(submesh.baseColor.w, 0.4f);
                    }
                    
                    if (mat.has_clearcoat) {
                        submesh.clearcoatFactor = mat.clearcoat.clearcoat_factor;
                        submesh.clearcoatRoughness = mat.clearcoat.clearcoat_roughness_factor;
                    }
                    
                    submesh.alphaCutoff = mat.alpha_cutoff;

                    // Load base color texture
                    if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
                        cgltf_image* img = pbr.base_color_texture.texture->image;
                        if (img->buffer_view && img->buffer_view->buffer->data) {
                            const uint8_t* imgData = (const uint8_t*)img->buffer_view->buffer->data + img->buffer_view->offset;
                            submesh.diffuseTextureSRV = CreateTextureFromMemory(device, context, imgData, img->buffer_view->size, true);
                            submesh.hasTexture = (submesh.diffuseTextureSRV != nullptr);
                            if (!submesh.hasTexture) std::cout << "[GLBLoader] Failed to load diffuse texture from memory!\n";
                        } else {
                            std::cout << "[GLBLoader] Texture image has no buffer_view!\n";
                        }
                    }
                    
                    // Load metallic-roughness texture
                    if (pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image) {
                        cgltf_image* img = pbr.metallic_roughness_texture.texture->image;
                        if (img->buffer_view && img->buffer_view->buffer->data) {
                            const uint8_t* imgData = (const uint8_t*)img->buffer_view->buffer->data + img->buffer_view->offset;
                            submesh.metallicRoughnessTextureSRV = CreateTextureFromMemory(device, context, imgData, img->buffer_view->size, false);
                            submesh.hasMetallicRoughnessMap = (submesh.metallicRoughnessTextureSRV != nullptr);
                        }
                    }
                }
                
                // Load normal texture
                if (mat.normal_texture.texture && mat.normal_texture.texture->image) {
                    cgltf_image* img = mat.normal_texture.texture->image;
                    if (img->buffer_view && img->buffer_view->buffer->data) {
                        const uint8_t* imgData = (const uint8_t*)img->buffer_view->buffer->data + img->buffer_view->offset;
                        submesh.normalTextureSRV = CreateTextureFromMemory(device, context, imgData, img->buffer_view->size, false);
                        submesh.hasNormalMap = (submesh.normalTextureSRV != nullptr);
                    }
                }
                
                // Load emissive texture
                if (mat.emissive_texture.texture && mat.emissive_texture.texture->image) {
                    cgltf_image* img = mat.emissive_texture.texture->image;
                    if (img->buffer_view && img->buffer_view->buffer->data) {
                        const uint8_t* imgData = (const uint8_t*)img->buffer_view->buffer->data + img->buffer_view->offset;
                        submesh.emissiveTextureSRV = CreateTextureFromMemory(device, context, imgData, img->buffer_view->size, true);
                        submesh.hasEmissiveMap = (submesh.emissiveTextureSRV != nullptr);
                        submesh.emissiveTexCoord = mat.emissive_texture.texcoord;
                    }
                }
            }

            outModel.submeshes.push_back(submesh);
        }
    }

    std::cout << "[GLBLoader] Total submeshes: " << outModel.submeshes.size() << "\n";
    std::cout << "[GLBLoader] Bounds: ("
              << outModel.boundsMin.x << "," << outModel.boundsMin.y << "," << outModel.boundsMin.z
              << ") to ("
              << outModel.boundsMax.x << "," << outModel.boundsMax.y << "," << outModel.boundsMax.z
              << ")\n";

    // Sort submeshes: Opaque (0) and Mask (1) first, Blend (2) last
    std::stable_sort(outModel.submeshes.begin(), outModel.submeshes.end(), 
        [](const SubMesh& a, const SubMesh& b) {
            return a.alphaMode < b.alphaMode;
        }
    );

    cgltf_free(data);
    return !outModel.submeshes.empty();
}
