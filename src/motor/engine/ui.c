#include <motor/engine/ui.h>

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <motor/base/math.h>
#include <motor/base/array.h>
#include <motor/base/util.h>
#include <motor/base/allocator.h>
#include <motor/graphics/renderer.h>
#include <motor/engine/asset_manager.h>
#include <motor/engine/assets/pipeline_asset.h>
#include <motor/engine/assets/font_asset.h>
#include "assets/font_asset.inl"

static FontAtlas *get_atlas(MtFontAsset *asset, uint32_t height);

struct MtUIRenderer
{
    MtAllocator *alloc;
    MtEngine *engine;

    MtPipelineAsset *pipeline;
    MtPipelineAsset *image_pipeline;

    MtFontAsset *font;
    uint32_t font_height;

    Vec2 pos;
    Vec3 color;

    MtViewport viewport;

    MtUIVertex *vertices;
    uint16_t *indices;
};

MtUIRenderer *mt_ui_create(MtAllocator *alloc, MtAssetManager *asset_manager)
{
    MtUIRenderer *ui = mt_alloc(alloc, sizeof(MtUIRenderer));
    memset(ui, 0, sizeof(*ui));

    ui->alloc  = alloc;
    ui->engine = asset_manager->engine;

    ui->pipeline =
        (MtPipelineAsset *)mt_asset_manager_load(asset_manager, "../assets/shaders/ui.glsl");
    assert(ui->pipeline);

    ui->image_pipeline =
        (MtPipelineAsset *)mt_asset_manager_load(asset_manager, "../assets/shaders/image.glsl");
    assert(ui->image_pipeline);

    ui->font_height = 20;
    ui->pos         = V2(0.0f, 0.0f);
    ui->color       = V3(1.0f, 1.0f, 1.0f);

    return ui;
}

void mt_ui_destroy(MtUIRenderer *ui)
{
    mt_free(ui->alloc, ui);
}

void mt_ui_set_font(MtUIRenderer *ui, MtFontAsset *font)
{
    ui->font = font;
}

void mt_ui_set_font_size(MtUIRenderer *ui, uint32_t height)
{
    ui->font_height = height;
}

void mt_ui_set_pos(MtUIRenderer *ui, Vec2 pos)
{
    ui->pos = pos;
}

void mt_ui_set_color(MtUIRenderer *ui, Vec3 color)
{
    ui->color = color;
}

static void draw_text(MtUIRenderer *ui, const char *text)
{
    FontAtlas *atlas = get_atlas(ui->font, ui->font_height);

    ui->pos.y += (float)ui->font_height;

    float last_x = 0.0f;

    uint32_t ci = 0;
    while (text[ci])
    {
        stbtt_packedchar *cd = &atlas->chardata[(uint32_t)text[ci]];

        uint16_t first_vertex = mt_array_size(ui->vertices);

        float x0 = (float)cd->x0 / (float)atlas->dim;
        float x1 = (float)cd->x1 / (float)atlas->dim;
        float y0 = (float)cd->y0 / (float)atlas->dim;
        float y1 = (float)cd->y1 / (float)atlas->dim;

        MtUIVertex vertices[4] = {
            {
                {last_x + ui->pos.x, ui->pos.y},
                {x0, y1},
                ui->color,
            }, // Bottom Left
            {
                {last_x + ui->pos.x, cd->yoff + ui->pos.y},
                {x0, y0},
                ui->color,
            }, // Top Left
            {
                {last_x + cd->xoff2 + ui->pos.x, cd->yoff + ui->pos.y},
                {x1, y0},
                ui->color,
            }, // Top Right
            {
                {last_x + cd->xoff2 + ui->pos.x, ui->pos.y},
                {x1, y1},
                ui->color,
            }, // Bottom Right
        };

        mt_array_push(ui->alloc, ui->vertices, vertices[0]);
        mt_array_push(ui->alloc, ui->vertices, vertices[1]);
        mt_array_push(ui->alloc, ui->vertices, vertices[2]);
        mt_array_push(ui->alloc, ui->vertices, vertices[3]);

        mt_array_push(ui->alloc, ui->indices, first_vertex);
        mt_array_push(ui->alloc, ui->indices, first_vertex + 1);
        mt_array_push(ui->alloc, ui->indices, first_vertex + 2);
        mt_array_push(ui->alloc, ui->indices, first_vertex + 2);
        mt_array_push(ui->alloc, ui->indices, first_vertex + 3);
        mt_array_push(ui->alloc, ui->indices, first_vertex);

        last_x += cd->xadvance;

        ci++;
    }
}

void mt_ui_printf(MtUIRenderer *ui, const char *fmt, ...)
{
    char buf[512];
    va_list vl;
    va_start(vl, fmt);
    vsnprintf(buf, sizeof(buf) - 1, fmt, vl);
    va_end(vl);

    buf[sizeof(buf) - 1] = '\0';

    draw_text(ui, buf);
}

void mt_ui_image(MtUIRenderer *ui, MtCmdBuffer *cb, MtImage *image)
{
    Mat4 transform = mat4_orthographic(
        0.0f, (float)ui->viewport.width, 0.0f, (float)ui->viewport.height, 0.0f, 1.0f);

    mt_render.cmd_bind_pipeline(cb, ui->image_pipeline->pipeline);

    mt_render.cmd_bind_uniform(cb, &transform, sizeof(transform), 0, 0);

    mt_render.cmd_bind_image(cb, image, ui->engine->default_sampler, 0, 1);

    float w = 512.0f;
    float h = 512.0f;

    MtUIVertex vertices[4] = {
        {{0.0f, 0.0f}, {0.0f, 0.0f}, {}},
        {{w, 0.0f}, {1.0f, 0.0f}, {}},
        {{w, h}, {1.0f, 1.0f}, {}},
        {{0.0f, h}, {0.0f, 1.0f}, {}},
    };

    uint16_t indices[6] = {0, 1, 2, 2, 3, 0};

    mt_render.cmd_bind_vertex_data(cb, vertices, sizeof(vertices));
    mt_render.cmd_bind_index_data(cb, indices, sizeof(indices), MT_INDEX_TYPE_UINT16);

    mt_render.cmd_draw_indexed(cb, MT_LENGTH(indices), 1, 0, 0, 0);
}

void mt_ui_begin(MtUIRenderer *ui, MtViewport *viewport)
{
    ui->viewport = *viewport;
    mt_array_set_size(ui->vertices, 0);
    mt_array_set_size(ui->indices, 0);
    ui->pos   = V2(0.0f, 0.0f);
    ui->color = V3(1.0f, 1.0f, 1.0f);
}

void mt_ui_draw(MtUIRenderer *ui, MtCmdBuffer *cb)
{
    Mat4 transform = mat4_orthographic(
        0.0f, (float)ui->viewport.width, 0.0f, (float)ui->viewport.height, 0.0f, 1.0f);

    mt_render.cmd_bind_pipeline(cb, ui->pipeline->pipeline);

    mt_render.cmd_bind_uniform(cb, &transform, sizeof(transform), 0, 0);

    FontAtlas *atlas = get_atlas(ui->font, ui->font_height);

    mt_render.cmd_bind_image(cb, atlas->image, ui->font->sampler, 0, 1);

    mt_render.cmd_bind_vertex_data(
        cb, ui->vertices, mt_array_size(ui->vertices) * sizeof(MtUIVertex));
    mt_render.cmd_bind_index_data(
        cb, ui->indices, mt_array_size(ui->indices) * sizeof(uint16_t), MT_INDEX_TYPE_UINT16);

    mt_render.cmd_draw_indexed(cb, mt_array_size(ui->indices), 1, 0, 0, 0);
}

static FontAtlas *get_atlas(MtFontAsset *asset, uint32_t height)
{
    FontAtlas *atlas = mt_hash_get_ptr(&asset->map, (uint64_t)height);
    if (!atlas)
    {
        MtAllocator *alloc = asset->asset_manager->alloc;

        // Create atlas
        mt_array_push(alloc, asset->atlases, (FontAtlas){0});
        atlas = mt_array_last(asset->atlases);
        memset(atlas, 0, sizeof(*atlas));

        atlas->dim = 2048;

        uint8_t *pixels = mt_alloc(alloc, atlas->dim * atlas->dim);

        stbtt_pack_context spc;
        int res = stbtt_PackBegin(&spc, pixels, atlas->dim, atlas->dim, 0, 1, NULL);
        if (!res)
        {
            return NULL;
        }

        stbtt_PackSetOversampling(&spc, 2, 2);

        int first_char  = 0;
        int char_count  = 255;
        atlas->chardata = mt_alloc(alloc, sizeof(stbtt_packedchar) * (char_count - first_char));

        res = stbtt_PackFontRange(
            &spc,
            asset->font_data,
            0,
            STBTT_POINT_SIZE((int32_t)height),
            first_char,
            char_count,
            atlas->chardata);
        if (!res)
        {
            return NULL;
        }

        stbtt_PackEnd(&spc);

        atlas->image = mt_render.create_image(
            asset->asset_manager->engine->device,
            &(MtImageCreateInfo){
                .width  = atlas->dim,
                .height = atlas->dim,
                .format = MT_FORMAT_R8_UNORM,
            });

        mt_render.transfer_to_image(
            asset->asset_manager->engine->device,
            &(MtImageCopyView){.image = atlas->image},
            atlas->dim * atlas->dim,
            pixels);

        mt_free(alloc, pixels);

        mt_hash_set_ptr(&asset->map, (uint64_t)height, atlas);
    }

    return atlas;
}
