#include <motor/base/math_types.h>

static const Vec3 g_cube_vertices[36] = {
    {-0.5, 0.5, -0.5},  {-0.5, -0.5, -0.5}, {0.5, -0.5, -0.5},
    {0.5, -0.5, -0.5},  {0.5, 0.5, -0.5},   {-0.5, 0.5, -0.5},

    {-0.5, -0.5, 0.5},  {-0.5, -0.5, -0.5}, {-0.5, 0.5, -0.5},
    {-0.5, 0.5, -0.5},  {-0.5, 0.5, 0.5},   {-0.5, -0.5, 0.5},

    {0.5, -0.5, -0.5},  {0.5, -0.5, 0.5},   {0.5, 0.5, 0.5},
    {0.5, 0.5, 0.5},    {0.5, 0.5, -0.5},   {0.5, -0.5, -0.5},

    {-0.5, -0.5, 0.5},  {-0.5, 0.5, 0.5},   {0.5, 0.5, 0.5},
    {0.5, 0.5, 0.5},    {0.5, -0.5, 0.5},   {-0.5, -0.5, 0.5},

    {-0.5, 0.5, -0.5},  {0.5, 0.5, -0.5},   {0.5, 0.5, 0.5},
    {0.5, 0.5, 0.5},    {-0.5, 0.5, 0.5},   {-0.5, 0.5, -0.5},

    {-0.5, -0.5, -0.5}, {-0.5, -0.5, 0.5},  {0.5, -0.5, -0.5},
    {0.5, -0.5, -0.5},  {-0.5, -0.5, 0.5},  {0.5, -0.5, 0.5},
};

static const Mat4 g_direction_matrices[6] = {
    {{
        {0.0, 0.0, -1.0, 0.0},
        {0.0, -1.0, 0.0, 0.0},
        {-1.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, 1.0},
    }},
    {{
        {0.0, 0.0, 1.0, 0.0},
        {0.0, -1.0, 0.0, 0.0},
        {1.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, 1.0},
    }},
    {{
        {1.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, -1.0, 0.0},
        {0.0, 1.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, 1.0},
    }},
    {{
        {1.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 1.0, 0.0},
        {0.0, -1.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, 1.0},
    }},
    {{
        {1.0, 0.0, 0.0, 0.0},
        {0.0, -1.0, 0.0, 0.0},
        {0.0, 0.0, -1.0, 0.0},
        {0.0, 0.0, 0.0, 1.0},
    }},
    {{
        {-1.0, 0.0, 0.0, 0.0},
        {0.0, -1.0, 0.0, 0.0},
        {0.0, 0.0, 1.0, 0.0},
        {0.0, 0.0, 0.0, 1.0},
    }},
};
