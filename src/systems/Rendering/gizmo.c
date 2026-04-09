
#include "../../../include/druid.h"
#include <math.h>
#include <GL/glew.h>

//=====================================================================================================================
// Gizmo internals — immediate-mode line batch renderer
//=====================================================================================================================

#define GIZMO_MAX_VERTICES 65536
#define GIZMO_CIRCLE_SEGMENTS 24

typedef struct
{
    Vec3 pos;
    f32  r, g, b, a;
} GizmoVertex;

static struct
{
    u32 vao;
    u32 vbo;
    u32 shader;
    GizmoVertex *vertices;
    u32 count;
    b8 initialized;
} gizmo;

static const c8 *gizmoVertSrc =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec4 aColor;\n"
    "uniform mat4 uViewProj;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "    gl_Position = uViewProj * vec4(aPos, 1.0);\n"
    "    vColor = aColor;\n"
    "}\n";

static const c8 *gizmoFragSrc =
    "#version 330 core\n"
    "in vec4 vColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    FragColor = vColor;\n"
    "}\n";

static u32 compileGizmoShader(void)
{
    u32 vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &gizmoVertSrc, NULL);
    glCompileShader(vert);

    u32 frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &gizmoFragSrc, NULL);
    glCompileShader(frag);

    u32 prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}

//=====================================================================================================================
// Lifecycle
//=====================================================================================================================

void gizmoInit(void)
{
    if (gizmo.initialized) return;

    gizmo.vertices = (GizmoVertex *)dalloc(sizeof(GizmoVertex) * GIZMO_MAX_VERTICES, MEM_TAG_RENDERER);
    gizmo.count = 0;

    glGenVertexArrays(1, &gizmo.vao);
    glGenBuffers(1, &gizmo.vbo);

    glBindVertexArray(gizmo.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gizmo.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GizmoVertex) * GIZMO_MAX_VERTICES, NULL, GL_DYNAMIC_DRAW);

    // pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GizmoVertex), (void *)0);
    // color
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GizmoVertex),
                          (void *)(3 * sizeof(f32)));

    glBindVertexArray(0);

    gizmo.shader = compileGizmoShader();
    gizmo.initialized = true;
}

void gizmoShutdown(void)
{
    if (!gizmo.initialized) return;
    glDeleteVertexArrays(1, &gizmo.vao);
    glDeleteBuffers(1, &gizmo.vbo);
    glDeleteProgram(gizmo.shader);
    dfree(gizmo.vertices, sizeof(GizmoVertex) * GIZMO_MAX_VERTICES, MEM_TAG_RENDERER);
    gizmo.initialized = false;
}

void gizmoBeginFrame(void)
{
    gizmo.count = 0;
}

void gizmoEndFrame(Mat4 viewProj)
{
    if (!gizmo.initialized || gizmo.count == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, gizmo.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(GizmoVertex) * gizmo.count, gizmo.vertices);

    glUseProgram(gizmo.shader);
    i32 loc = glGetUniformLocation(gizmo.shader, "uViewProj");
    glUniformMatrix4fv(loc, 1, GL_FALSE, &viewProj.m[0][0]);

    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(gizmo.vao);
    glDrawArrays(GL_LINES, 0, gizmo.count);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);

    gizmo.count = 0;
}

//=====================================================================================================================
// Internal helpers
//=====================================================================================================================

static void pushVertex(Vec3 pos, GizmoColor c)
{
    if (gizmo.count >= GIZMO_MAX_VERTICES) return;
    GizmoVertex *v = &gizmo.vertices[gizmo.count++];
    v->pos = pos;
    v->r = c.r; v->g = c.g; v->b = c.b; v->a = c.a;
}

static void pushLine(Vec3 a, Vec3 b, GizmoColor c)
{
    pushVertex(a, c);
    pushVertex(b, c);
}

//=====================================================================================================================
// Primitives
//=====================================================================================================================

void gizmoDrawLine(Vec3 from, Vec3 to, GizmoColor color)
{
    pushLine(from, to, color);
}

void gizmoDrawRay(Vec3 origin, Vec3 direction, f32 length, GizmoColor color)
{
    Vec3 end = v3Add(origin, v3Scale(direction, length));
    pushLine(origin, end, color);
}

void gizmoDrawArrow(Vec3 from, Vec3 to, f32 headSize, GizmoColor color)
{
    pushLine(from, to, color);

    Vec3 dir = v3Norm(v3Sub(to, from));
    Vec3 up = (fabsf(dir.y) < 0.99f) ? (Vec3){0, 1, 0} : (Vec3){1, 0, 0};
    Vec3 right = v3Norm(v3Cross(dir, up));
    Vec3 fwd = v3Cross(right, dir);

    Vec3 base = v3Sub(to, v3Scale(dir, headSize));
    pushLine(to, v3Add(base, v3Scale(right, headSize * 0.4f)), color);
    pushLine(to, v3Sub(base, v3Scale(right, headSize * 0.4f)), color);
    pushLine(to, v3Add(base, v3Scale(fwd, headSize * 0.4f)), color);
    pushLine(to, v3Sub(base, v3Scale(fwd, headSize * 0.4f)), color);
}

//=====================================================================================================================
// Shapes
//=====================================================================================================================

void gizmoDrawSphere(Vec3 center, f32 radius, GizmoColor color)
{
    for (u32 i = 0; i < GIZMO_CIRCLE_SEGMENTS; i++)
    {
        f32 a0 = (f32)i / GIZMO_CIRCLE_SEGMENTS * 2.0f * PI;
        f32 a1 = (f32)(i + 1) / GIZMO_CIRCLE_SEGMENTS * 2.0f * PI;
        f32 c0 = cosf(a0) * radius, s0 = sinf(a0) * radius;
        f32 c1 = cosf(a1) * radius, s1 = sinf(a1) * radius;

        // XY circle
        pushLine((Vec3){center.x + c0, center.y + s0, center.z},
                 (Vec3){center.x + c1, center.y + s1, center.z}, color);
        // XZ circle
        pushLine((Vec3){center.x + c0, center.y, center.z + s0},
                 (Vec3){center.x + c1, center.y, center.z + s1}, color);
        // YZ circle
        pushLine((Vec3){center.x, center.y + c0, center.z + s0},
                 (Vec3){center.x, center.y + c1, center.z + s1}, color);
    }
}

void gizmoDrawBox(Vec3 center, Vec3 half, GizmoColor color)
{
    Vec3 c = center;
    Vec3 h = half;

    Vec3 corners[8] = {
        {c.x - h.x, c.y - h.y, c.z - h.z},
        {c.x + h.x, c.y - h.y, c.z - h.z},
        {c.x + h.x, c.y + h.y, c.z - h.z},
        {c.x - h.x, c.y + h.y, c.z - h.z},
        {c.x - h.x, c.y - h.y, c.z + h.z},
        {c.x + h.x, c.y - h.y, c.z + h.z},
        {c.x + h.x, c.y + h.y, c.z + h.z},
        {c.x - h.x, c.y + h.y, c.z + h.z},
    };

    // bottom
    pushLine(corners[0], corners[1], color); pushLine(corners[1], corners[2], color);
    pushLine(corners[2], corners[3], color); pushLine(corners[3], corners[0], color);
    // top
    pushLine(corners[4], corners[5], color); pushLine(corners[5], corners[6], color);
    pushLine(corners[6], corners[7], color); pushLine(corners[7], corners[4], color);
    // pillars
    pushLine(corners[0], corners[4], color); pushLine(corners[1], corners[5], color);
    pushLine(corners[2], corners[6], color); pushLine(corners[3], corners[7], color);
}

void gizmoDrawCylinder(Vec3 center, f32 radius, f32 halfHeight, GizmoColor color)
{
    Vec3 top = {center.x, center.y + halfHeight, center.z};
    Vec3 bot = {center.x, center.y - halfHeight, center.z};

    for (u32 i = 0; i < GIZMO_CIRCLE_SEGMENTS; i++)
    {
        f32 a0 = (f32)i / GIZMO_CIRCLE_SEGMENTS * 2.0f * PI;
        f32 a1 = (f32)(i + 1) / GIZMO_CIRCLE_SEGMENTS * 2.0f * PI;
        f32 c0 = cosf(a0) * radius, s0 = sinf(a0) * radius;
        f32 c1 = cosf(a1) * radius, s1 = sinf(a1) * radius;

        // top circle
        pushLine((Vec3){top.x + c0, top.y, top.z + s0},
                 (Vec3){top.x + c1, top.y, top.z + s1}, color);
        // bottom circle
        pushLine((Vec3){bot.x + c0, bot.y, bot.z + s0},
                 (Vec3){bot.x + c1, bot.y, bot.z + s1}, color);
    }

    // 4 vertical lines
    for (u32 i = 0; i < 4; i++)
    {
        f32 a = (f32)i / 4.0f * 2.0f * PI;
        f32 cx = cosf(a) * radius, cz = sinf(a) * radius;
        pushLine((Vec3){center.x + cx, bot.y, center.z + cz},
                 (Vec3){center.x + cx, top.y, center.z + cz}, color);
    }
}

void gizmoDrawAABB(AABB box, GizmoColor color)
{
    Vec3 center = v3Scale(v3Add(box.min, box.max), 0.5f);
    Vec3 half = v3Scale(v3Sub(box.max, box.min), 0.5f);
    gizmoDrawBox(center, half, color);
}

void gizmoDrawCircle(Vec3 center, Vec3 normal, f32 radius, GizmoColor color)
{
    Vec3 n = v3Norm(normal);
    Vec3 up = (fabsf(n.y) < 0.99f) ? (Vec3){0, 1, 0} : (Vec3){1, 0, 0};
    Vec3 right = v3Norm(v3Cross(n, up));
    Vec3 fwd = v3Cross(right, n);

    for (u32 i = 0; i < GIZMO_CIRCLE_SEGMENTS; i++)
    {
        f32 a0 = (f32)i / GIZMO_CIRCLE_SEGMENTS * 2.0f * PI;
        f32 a1 = (f32)(i + 1) / GIZMO_CIRCLE_SEGMENTS * 2.0f * PI;
        Vec3 p0 = v3Add(center, v3Add(v3Scale(right, cosf(a0) * radius),
                                       v3Scale(fwd, sinf(a0) * radius)));
        Vec3 p1 = v3Add(center, v3Add(v3Scale(right, cosf(a1) * radius),
                                       v3Scale(fwd, sinf(a1) * radius)));
        pushLine(p0, p1, color);
    }
}

//=====================================================================================================================
// Grids & axes
//=====================================================================================================================

void gizmoDrawGrid(Vec3 center, f32 size, u32 divisions, GizmoColor color)
{
    f32 half = size * 0.5f;
    f32 step = size / (f32)divisions;

    for (u32 i = 0; i <= divisions; i++)
    {
        f32 offset = -half + step * (f32)i;
        pushLine((Vec3){center.x + offset, center.y, center.z - half},
                 (Vec3){center.x + offset, center.y, center.z + half}, color);
        pushLine((Vec3){center.x - half, center.y, center.z + offset},
                 (Vec3){center.x + half, center.y, center.z + offset}, color);
    }
}

void gizmoDrawAxes(Vec3 origin, f32 length)
{
    gizmoDrawArrow(origin, v3Add(origin, (Vec3){length, 0, 0}), length * 0.1f, GIZMO_RED);
    gizmoDrawArrow(origin, v3Add(origin, (Vec3){0, length, 0}), length * 0.1f, GIZMO_GREEN);
    gizmoDrawArrow(origin, v3Add(origin, (Vec3){0, 0, length}), length * 0.1f, GIZMO_BLUE);
}

void gizmoDrawTransform(Transform *t, f32 length)
{
    if (!t) return;
    Vec3 right = quatRotateVec3(t->rot, (Vec3){1, 0, 0});
    Vec3 up    = quatRotateVec3(t->rot, (Vec3){0, 1, 0});
    Vec3 fwd   = quatRotateVec3(t->rot, (Vec3){0, 0, -1});

    gizmoDrawArrow(t->pos, v3Add(t->pos, v3Scale(right, length)), length * 0.1f, GIZMO_RED);
    gizmoDrawArrow(t->pos, v3Add(t->pos, v3Scale(up, length)),    length * 0.1f, GIZMO_GREEN);
    gizmoDrawArrow(t->pos, v3Add(t->pos, v3Scale(fwd, length)),   length * 0.1f, GIZMO_BLUE);
}

//=====================================================================================================================
// Physics debug
//=====================================================================================================================

void gizmoDrawCollider(Collider *col, Vec3 pos, GizmoColor color)
{
    if (!col || !col->state) return;

    switch (col->type)
    {
    case COLLIDER_SPHERE:
        gizmoDrawSphere(pos, getRadius(col), color);
        break;
    case COLLIDER_BOX:
        gizmoDrawBox(pos, getHalfExtents(col), color);
        break;
    case COLLIDER_CYLINDER:
    {
        f32 r = getRadius(col);
        // Access half height through state
        typedef struct { f32 radius; f32 halfHeight; } CylState;
        CylState *cs = (CylState *)col->state;
        gizmoDrawCylinder(pos, r, cs->halfHeight, color);
    } break;
    case COLLIDER_MESH:
        gizmoDrawAABB(colliderComputeAABB(col, pos), color);
        break;
    default:
        break;
    }
}

void gizmoDrawContactManifold(const ContactManifold *manifold, GizmoColor color)
{
    if (!manifold) return;
    for (u32 i = 0; i < manifold->pointCount; i++)
    {
        Vec3 p = manifold->points[i].point;
        Vec3 n = manifold->points[i].normal;
        gizmoDrawSphere(p, 0.02f, color);
        gizmoDrawArrow(p, v3Add(p, v3Scale(n, 0.2f)), 0.03f, color);
    }
}
