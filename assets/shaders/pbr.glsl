blending = true
depth_test = true
depth_write = true
cull_mode = "front"
front_face = "clockwise"

vertex = @{
    #include "common.glsl"

    layout (location = 0) in vec3 pos;
    layout (location = 1) in vec3 normal;
    layout (location = 2) in vec2 tex_coords;
    layout (location = 3) in vec4 tangent;

    layout (set = 0, binding = 0) uniform CameraUniform {
        Camera cam;
    };

    layout (set = 1, binding = 0) uniform ModelUniform {
        Model model;
    };

    layout (location = 0) out vec2 tex_coords0;
    layout (location = 1) out vec3 world_pos;
    layout (location = 2) out vec3 normal0;
    layout (location = 3) out mat3 TBN;

    void main() {
        tex_coords0 = tex_coords;

        mat4 model0 = model.model * model.local_model;

        if (model.normal_mapped == 1.0f) {
            vec3 T = normalize(vec3(model0 * vec4(tangent.xyz, 0.0f)));
            vec3 N = normalize(vec3(model0 * vec4(normal.xyz, 0.0f)));
            T = normalize(T - dot(T, N) * N); // re-orthogonalize
            vec3 B = tangent.w * cross(N, T);
            TBN = mat3(T, B, N);
        } else {
            normal0 = mat3(transpose(inverse(model0))) * normal;
        }

        vec4 loc_pos =  model0 * vec4(pos, 1.0);

        world_pos = loc_pos.xyz / loc_pos.w;

        gl_Position = cam.proj * cam.view * vec4(world_pos, 1.0f);
    }
}@

fragment = @{
    #include "common.glsl"
    #include "pbr_common.glsl"

    layout (location = 0) in vec2 tex_coords;
    layout (location = 1) in vec3 world_pos;
    layout (location = 2) in vec3 normal;
    layout (location = 3) in mat3 TBN;

    layout (set = 0, binding = 0) uniform CameraUniform {
        Camera cam;
    };

    layout (set = 1, binding = 0) uniform ModelUniform {
        Model model;
    };

    layout (set = 2, binding = 0) uniform MaterialUniform {
        Material material;
    };
    layout (set = 2, binding = 1) uniform sampler2D albedo_texture;
    layout (set = 2, binding = 2) uniform sampler2D normal_texture;
    layout (set = 2, binding = 3) uniform sampler2D metallic_roughness_texture;
    layout (set = 2, binding = 4) uniform sampler2D occlusion_texture;
    layout (set = 2, binding = 5) uniform sampler2D emissive_texture;

    layout (set = 3, binding = 0) uniform EnvironmentUniform {
        Environment environment;
    };
    layout (set = 3, binding = 1) uniform samplerCube irradiance_map;
    layout (set = 3, binding = 2) uniform samplerCube radiance_map;
    layout (set = 3, binding = 3) uniform sampler2D brdf_lut;

    layout (location = 0) out vec4 out_color;
    
    void main() {
        vec3 V = normalize(cam.pos.xyz - world_pos);

        vec3 N;

        if (model.normal_mapped == 1.0f) {
            N = texture(normal_texture, tex_coords).rgb;
            N = normalize(N * 2.0 - 1.0); // Remap from [0, 1] to [-1, 1]
            N = normalize(TBN * N);
        } else {
            N = normalize(normal);
        }

        vec4 albedo = srgb_to_linear(texture(albedo_texture, tex_coords)) * material.base_color;

        vec4 metallic_roughness = texture(metallic_roughness_texture, tex_coords);
        float metallic = material.metallic * metallic_roughness.b;
        float roughness = material.roughness * metallic_roughness.g;

        float occlusion = texture(occlusion_texture, tex_coords).r;
        vec3 emissive = srgb_to_linear(texture(emissive_texture, tex_coords)).rgb * material.emissive.rgb;

        // Calculate PBR

        vec3 R = -normalize(reflect(V, N));

        float NdotV = clamp(abs(dot(N, V)), 0.001, 1.0);

        vec3 F0 = vec3(0.04); 
        F0 = mix(F0, albedo.rgb, metallic);

        vec3 Lo = vec3(0.0);

        // Directional light (sun)
        {
            vec3 L = normalize(-environment.sun_direction);
            vec3 H = normalize(V + L);
            vec3 radiance = environment.sun_color * environment.sun_intensity;

            float NdotL = clamp(dot(N, L), 0.001, 1.0);
            float VdotH = clamp(dot(H, V), 0.0, 1.0);

            // Cook-Torrance BRDF
            float NDF = distribution_ggx(N, H, roughness);
            float G = geometry_schlick_smith_ggx(NdotL, NdotV, roughness);
            vec3 F = fresnel_schlick(VdotH, F0);

            vec3 nominator = NDF * G * F;
            float denominator = 4.0 * NdotV * NdotL + 0.001;
            vec3 specular = nominator / denominator;

            vec3 kS = F;
            vec3 kD = vec3(1.0) - kS;
            kD *= 1.0 - metallic;

            Lo += (kD * albedo.rgb / PI + specular) * radiance * NdotL;
        }

        // Point lights
        for (int i = 0; i < environment.light_count; i++) {
            // Calculate per-light radiance
            vec3 light_pos = environment.point_lights[i].pos.xyz;
            vec3 L = normalize(light_pos - world_pos);
            vec3 H = normalize(V + L);
            float distance = length(light_pos - world_pos);
            float attenuation = 1.0 / (distance * distance);
            vec3 radiance = environment.point_lights[i].color.rgb * attenuation;

            float NdotL = clamp(dot(N, L), 0.001, 1.0);
            float VdotH = clamp(dot(H, V), 0.0, 1.0);

            // Cook-Torrance BRDF
            float NDF = distribution_ggx(N, H, roughness);
            float G = geometry_schlick_smith_ggx(NdotL, NdotV, roughness);
            vec3 F = fresnel_schlick(VdotH, F0);

            vec3 nominator = NDF * G * F;
            float denominator = 4.0 * NdotV * NdotL + 0.001;
            vec3 specular = nominator / denominator;

            vec3 kS = F;
            vec3 kD = vec3(1.0) - kS;
            kD *= 1.0 - metallic;

            Lo += (kD * albedo.rgb / PI + specular) * radiance * NdotL;
        }

        vec3 F = fresnel_schlick_roughness(max(dot(N, V), 0.0), F0, roughness);

        vec3 kS = F;
        vec3 kD = 1.0 - kS;
        kD *= 1.0 - metallic;

        vec3 irradiance = tonemap(srgb_to_linear(texture(irradiance_map, N)), environment.exposure).rgb;
        vec3 diffuse = irradiance * albedo.rgb;

        vec3 prefiltered_color = 
                    tonemap(srgb_to_linear(textureLod(radiance_map, R, roughness * environment.radiance_mip_levels)), environment.exposure).rgb;
        vec2 brdf = srgb_to_linear(texture(brdf_lut, vec2(max(dot(N, V), 0.0), roughness))).rg;
        vec3 specular = prefiltered_color * (F * brdf.x + brdf.y);

        vec3 ambient = kD * diffuse + specular;

        out_color = vec4((ambient + Lo) * occlusion + emissive, 1.0);
    }
}@
