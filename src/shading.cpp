#include "shading.h"

#include "raylib.h"
#include "rlgl.h"

namespace {

// The scene's one light, in world space: over the viewer's left shoulder. The
// camera looks down -Z at the bomb, so this rakes across whichever face is
// turned forward -- enough to model it, without turning it away from the light
// as soon as the players rotate the bomb.
constexpr float light_x = -0.38f;
constexpr float light_y = 0.62f;
constexpr float light_z = 0.78f;

// Desktop GL and WebGL want different dialects of the same shader; which one is
// live is a property of the context, not of the platform we compiled for, so
// ask rlgl rather than branching on an #ifdef.
bool wants_glsl_100() {
    const int version = rlGetVersion();
    return version == RL_OPENGL_ES_20 || version == RL_OPENGL_ES_30;
}

// Vertex stage (both dialects): pass the world-space position and normal rlgl
// has already produced straight through, and project with the batch's mvp.
const char* vs_330 = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragPosition;
out vec3 fragNormal;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragPosition = vertexPosition;
    fragNormal = vertexNormal;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

const char* vs_100 = R"(#version 100
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec3 vertexNormal;
attribute vec4 vertexColor;

uniform mat4 mvp;

varying vec2 fragTexCoord;
varying vec4 fragColor;
varying vec3 fragPosition;
varying vec3 fragNormal;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragPosition = vertexPosition;
    fragNormal = vertexNormal;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

// Fragment stage: ambient + lambert diffuse + Blinn specular, over the module
// texture (or the flat white default texture for untextured geometry).
//
// `fill` is a diffuse term from the camera rather than the light. It is not
// physical; it is what lets the ambient floor stay low enough to model the
// casing while any face turned towards the viewer -- which for a module is
// every face they are actually trying to read -- stays bright.
const char* fs_330 = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPosition;
in vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 lightDir;
uniform vec4 material;   // ambient, diffuse, specular, shininess
uniform float specTint;

out vec4 finalColor;

void main()
{
    vec4 base = texture(texture0, fragTexCoord)*colDiffuse*fragColor;

    vec3 n = normalize(fragNormal);
    vec3 v = normalize(viewPos - fragPosition);
    if (dot(n, v) < 0.0) n = -n;
    vec3 l = normalize(lightDir);

    float diff = max(dot(n, l), 0.0);
    float fill = 0.34*max(dot(n, v), 0.0);

    float spec = 0.0;
    if (diff > 0.0)
    {
        vec3 h = normalize(l + v);
        spec = pow(max(dot(n, h), 0.0), max(material.w, 1.0))*material.z;
    }

    vec3 tint = mix(vec3(1.0), base.rgb, specTint);
    vec3 col = base.rgb*(material.x + material.y*diff + fill) + tint*spec;
    finalColor = vec4(min(col, vec3(1.0)), base.a);
}
)";

const char* fs_100 = R"(#version 100
precision mediump float;

varying vec2 fragTexCoord;
varying vec4 fragColor;
varying vec3 fragPosition;
varying vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 lightDir;
uniform vec4 material;   // ambient, diffuse, specular, shininess
uniform float specTint;

void main()
{
    vec4 base = texture2D(texture0, fragTexCoord)*colDiffuse*fragColor;

    vec3 n = normalize(fragNormal);
    vec3 v = normalize(viewPos - fragPosition);
    if (dot(n, v) < 0.0) n = -n;
    vec3 l = normalize(lightDir);

    float diff = max(dot(n, l), 0.0);
    float fill = 0.34*max(dot(n, v), 0.0);

    float spec = 0.0;
    if (diff > 0.0)
    {
        vec3 h = normalize(l + v);
        spec = pow(max(dot(n, h), 0.0), max(material.w, 1.0))*material.z;
    }

    vec3 tint = mix(vec3(1.0), base.rgb, specTint);
    vec3 col = base.rgb*(material.x + material.y*diff + fill) + tint*spec;
    gl_FragColor = vec4(min(col, vec3(1.0)), base.a);
}
)";

}   // namespace

void PhongShader::load() {
    if (loaded_) return;

    const bool es = wants_glsl_100();
    shader_ = LoadShaderFromMemory(es ? vs_100 : vs_330, es ? fs_100 : fs_330);
    if (shader_.id == 0 || shader_.id == rlGetShaderIdDefault()) return;

    loc_view_pos_ = GetShaderLocation(shader_, "viewPos");
    loc_light_dir_ = GetShaderLocation(shader_, "lightDir");
    loc_material_ = GetShaderLocation(shader_, "material");
    loc_spec_tint_ = GetShaderLocation(shader_, "specTint");
    loaded_ = true;
}

void PhongShader::unload() {
    if (!loaded_) return;
    UnloadShader(shader_);
    shader_ = Shader{};
    loaded_ = false;
}

void PhongShader::begin(Vector3 view_pos) const {
    if (!loaded_) return;
    BeginShaderMode(shader_);

    const float eye[3] = {view_pos.x, view_pos.y, view_pos.z};
    SetShaderValue(shader_, loc_view_pos_, eye, SHADER_UNIFORM_VEC3);

    const float dir[3] = {light_x, light_y, light_z};
    SetShaderValue(shader_, loc_light_dir_, dir, SHADER_UNIFORM_VEC3);

    set_material(SurfaceMaterial{});
}

void PhongShader::end() const {
    if (!loaded_) return;
    EndShaderMode();
}

void PhongShader::set_material(const SurfaceMaterial& m) const {
    if (!loaded_) return;

    // Draw what is already recorded before the uniforms change under it: rlgl
    // batches geometry and applies whatever uniforms are set when the batch is
    // finally flushed, so without this every object in the batch would take the
    // last material set.
    rlDrawRenderBatchActive();

    const float params[4] = {m.ambient, m.diffuse, m.specular, m.shininess};
    SetShaderValue(shader_, loc_material_, params, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader_, loc_spec_tint_, &m.spec_tint, SHADER_UNIFORM_FLOAT);
}
