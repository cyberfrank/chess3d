#include "foundation/log.h"
#include "components.h"
#include "entity.h"
#include "serialize.h"
#include "render/mesh.h"
#include "render/visibility_mask.h"
#include "render/material.h"
#include "render/gfx_api.h"
#include "render/shader_repo.h"

static void serialize_transform(serializer_o *s, transform_t *data)
{
    emit_comment(s, "position");
    emit_vec3(s, data->pos);
    emit_comment(s, "rotation");
    emit_vec4(s, data->rot);
    emit_comment(s, "scale");
    emit_vec3(s, data->scl);
}

static void serialize_light(serializer_o *s, light_component_t *light)
{
    emit_comment(s, "light");
    emit_int(s, light->type);
    emit_comment(s, "color");
    emit_vec3(s, light->color);
    emit_float(s, light->intensity);

    if (light->type == LIGHT_TYPE_SPOT) {
        emit_comment(s, "spot_angle");
        emit_float(s, light->spot_angle_inner);
        emit_float(s, light->spot_angle_outer);
    }
}

static void serialize_volume(serializer_o *s, volume_component_t *volume)
{
    emit_comment(s, "volume");
    emit_comment(s, "min");
    emit_vec3(s, volume->bb_min);
    emit_comment(s, "max");
    emit_vec3(s, volume->bb_max);
    emit_comment(s, "blend");
    emit_float(s, volume->blend_distance);
}

static void serialize_mesh(serializer_o *s, mesh_component_t *mesh)
{
    emit_comment(s, "mesh");
    emit_string(s, mesh->mesh_path, 64);
    emit_comment(s, "materials");
    emit_int(s, mesh->num_materials);
    for (uint32_t i = 0; i < mesh->num_materials; ++i) {
        emit_string(s, mesh->material_path[i], 64);
    }
}

static void serialize_piece(serializer_o *s, piece_component_t *piece)
{
    emit_comment(s, "piece");
    emit_int(s, piece->mask);
}

static void serialize_tile(serializer_o *s, tile_component_t *tile)
{
    emit_comment(s, "tile");
}

static void serialize_board(serializer_o *s, board_component_t *board)
{
    emit_comment(s, "board");
}

static void load_mesh_component(entity_ctx_o *ctx, entity_t owner, mesh_component_t *c)
{
    extern struct asset_catalog_t *meshes;
    extern struct asset_catalog_t *materials;

    mesh_t *mesh = load_mesh_from_file(meshes, c->mesh_path);
    if (mesh == 0) {
        log_print(LOG_ERROR, "Failed to load mesh component '%s'", c->mesh_path);
        return;
    }
    c->data = mesh;

    if (c->num_materials > 0 && mesh->num_wanted_materials != c->num_materials) {
        log_print(LOG_WARN, "Expected %i materials but got %i for mesh '%s'", 
            mesh->num_wanted_materials, c->num_materials, c->mesh_path);
    }

    for (uint32_t m = 0; m < mesh->num_wanted_materials; ++m) {
        material_t *mat = (m < c->num_materials ? load_material_from_file(materials, c->material_path[m]) : default_material);
        c->materials[m] = *mat;
    }
}

void register_all_components(entity_ctx_o *ctx)
{
    component_i *transform = &(component_i) {
        .component_size = sizeof(transform_t),
        .default_data = &(transform_t) {
            .pos = make_vec3(0, 0, 0),
            .rot = make_vec4(0, 0, 0, 1),
            .scl = make_vec3(1, 1, 1),
        },
        .name = "Transform",
        .serialize_func = serialize_transform,
    };

    component_i *light = &(component_i) {
        .component_size = sizeof(light_component_t),
        .default_data = &(light_component_t) {
            .enabled = true,
            .type = LIGHT_TYPE_POINT,
            .color = make_vec3(1, 1, 1),
            .intensity = 1.0f,
        },
        .name = "Light Component",
        .serialize_func = serialize_light,
    };

    component_i *volume = &(component_i) {
        .component_size = sizeof(volume_component_t),
        .name = "Volume Component",
        .serialize_func = serialize_volume,
    };

    component_i *mesh = &(component_i) {
        .component_size = sizeof(mesh_component_t),
        .default_data = &(mesh_component_t) {
            .visibility_mask = VIEWER_MASK_MAIN | VIEWER_MASK_SHADOW,
        },
        .name = "Mesh Component",
        .load_func = load_mesh_component,
        .serialize_func = serialize_mesh,
    };

    component_i *piece = &(component_i) {
        .component_size = sizeof(piece_component_t),
        .name = "Piece Component",
        .serialize_func = serialize_piece,
    };

    component_i *tile = &(component_i) {
        .component_size = sizeof(tile_component_t),
        .name = "Tile Component",
        .serialize_func = serialize_tile,
    };

    component_i *board = &(component_i) {
        .component_size = sizeof(board_component_t),
        .name = "Board Component",
        .default_data = &(board_component_t) {
            .indices = {
                0xe, 0xa, 0xd, 0xb, 0xf, 0xd, 0xa, 0xe, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				0x6, 0x2, 0x5, 0x3, 0x7, 0x5, 0x2, 0x6, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
            },
            .selected_piece = (entity_t) { .id = UINT64_MAX },
            .castle_bits = 0xf,
            .current_player = 0x0,
            .move_count = 0,
        },
        .serialize_func = serialize_board,
    };

    transform_id = register_component_type(ctx, transform);
    volume_id =    register_component_type(ctx, volume);
    piece_id =     register_component_type(ctx, piece);
    light_id =     register_component_type(ctx, light);
    mesh_id =      register_component_type(ctx, mesh);
    tile_id =      register_component_type(ctx, tile);
    board_id =     register_component_type(ctx, board);
}