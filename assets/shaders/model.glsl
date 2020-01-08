blending: true
depth_test: true
depth_write: true
cull_mode: "front"
front_face: "counter_clockwise"

common: [[
    struct Camera {
        mat4 view;
        mat4 proj;
        vec4 pos;
    };

    struct Material {
        vec4 base_color;
        float metallic;
        float roughness;
        vec4 emissive;
    };
]]

vertex: [[
    layout (location = 0) in vec3 pos;
    layout (location = 1) in vec3 normal;
    layout (location = 2) in vec3 tangent;
    layout (location = 3) in vec2 tex_coords;

    layout (set = 0, binding = 0) uniform CameraUniform {
        Camera cam;
    };

    layout (set = 1, binding = 0) uniform ModelUniform {
        mat4 local_model;
        mat4 model;
    };

    layout (location = 0) out vec3 normal0;
    layout (location = 1) out vec2 tex_coords0;

    void main() {
        normal0 = normal;
        tex_coords0 = tex_coords;
        tex_coords0.y = 1.0f - tex_coords0.y;

        mat4 model0 = model * local_model;

        vec4 loc_pos =  model0 * vec4(pos, 1.0f);
        vec3 world_pos = loc_pos.xyz / loc_pos.w;

        gl_Position = cam.proj * cam.view * vec4(world_pos, 1.0f);
    }
]]

fragment: [[
    layout (location = 0) in vec3 normal;
    layout (location = 1) in vec2 tex_coords;

    layout (set = 2, binding = 0) uniform MaterialUniform {
        Material material;
    };
    layout (set = 2, binding = 1) uniform sampler2D albedo_texture;
    layout (set = 2, binding = 2) uniform sampler2D normal_texture;
    layout (set = 2, binding = 3) uniform sampler2D metallic_roughness_texture;
    layout (set = 2, binding = 4) uniform sampler2D occlusion_texture;
    layout (set = 2, binding = 5) uniform sampler2D emissive_texture;

    layout (location = 0) out vec4 out_color;
    
    void main() {
        out_color = texture(albedo_texture, tex_coords);
    }
]]
