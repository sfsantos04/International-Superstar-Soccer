/* actor3d.c - the 3D actor that replaces a player sprite, engine-side.
 *
 * Design constraints that come from the port, not from taste:
 *   - It must be plain C99 with no allocation and no dependencies, because it
 *     runs inside the frame loop (target: well under 1 ms for 21 players).
 *   - It rasterises into a small buffer (96x64) because the cartridge sprite
 *     budget is that size; the caller composites it with the sprite stencil.
 *   - Pose is driven by a phase in [0,1), not by wall time, so the caller can
 *     sync it to the game's own animation clock instead of drifting.
 *
 * Build standalone test: cc -O2 -o actor3d_test actor3d.c -lm -DACTOR3D_TEST
 */
#include <string.h>
#include <math.h>

#define A3_W 96
#define A3_H 64
#define A3_MAXBONES 14

typedef struct { float m[3][3]; float t[3]; } a3_xform;   /* rotate + translate */
typedef struct { float x, y, z; } a3_vec;

typedef struct {
    int   parent;
    float pivot[3];     /* joint position in rest space */
    a3_xform pose;      /* animated local transform */
} a3_bone;

typedef struct {
    /* mesh: 8 vertices per box, 12 triangles per box */
    int    boxes;
    float  verts[80][3];   /* up to 8 boxes: body, head, 2 arms, 2 legs */
    int    box_part[10];
    unsigned char box_rgb[10][3];
    a3_bone bones[A3_MAXBONES];
    int    bone_count;
    a3_xform world[A3_MAXBONES];
} a3_actor;

/* ---- small helpers ---------------------------------------------------- */

static void xf_identity(a3_xform *x) {
    memset(x, 0, sizeof *x);
    x->m[0][0] = x->m[1][1] = x->m[2][2] = 1.0f;
}

static void xf_rot_x(a3_xform *x, float a) {
    float c = cosf(a), s = sinf(a);
    xf_identity(x);
    x->m[1][1] = c; x->m[1][2] = -s;
    x->m[2][1] = s; x->m[2][2] = c;
}
static void xf_rot_y(a3_xform *x, float a) {
    float c = cosf(a), s = sinf(a);
    xf_identity(x);
    x->m[0][0] = c;  x->m[0][2] = s;
    x->m[2][0] = -s; x->m[2][2] = c;
}
/* b = a * b  (a applied after b) */
static void xf_mul(a3_xform *out, const a3_xform *a, const a3_xform *b) {
    a3_xform r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            r.m[i][j] = a->m[i][0]*b->m[0][j] + a->m[i][1]*b->m[1][j] + a->m[i][2]*b->m[2][j];
        r.t[i] = a->m[i][0]*b->t[0] + a->m[i][1]*b->t[1] + a->m[i][2]*b->t[2] + a->t[i];
    }
    *out = r;
}

static void xf_point(const a3_xform *x, const float p[3], float out[3]) {
    for (int i = 0; i < 3; i++)
        out[i] = x->m[i][0]*p[0] + x->m[i][1]*p[1] + x->m[i][2]*p[2] + x->t[i];
}

/* ---- body ------------------------------------------------------------- */

#define BOX_BODY  0
#define BOX_HEAD  1
#define BOX_LARM  2
#define BOX_RARM  3
#define BOX_LLEG  4
#define BOX_RLEG  5
#define BOX_LSHIN 6
#define BOX_RSHIN 7

static const int bone_of_box[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 7, 8 };

static void add_box(a3_actor *a, float cx, float cy, float cz,
                    float hx, float hy, float hz, unsigned char r,
                    unsigned char g, unsigned char b) {
    static const float unit[8][3] = {
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1, 1},{1,-1, 1},{1,1, 1},{-1,1, 1}
    };
    int v = a->boxes * 8;
    for (int i = 0; i < 8; i++) {
        a->verts[v+i][0] = cx + unit[i][0]*hx;
        a->verts[v+i][1] = cy + unit[i][1]*hy;
        a->verts[v+i][2] = cz + unit[i][2]*hz;
    }
    a->box_rgb[v/8][0] = r; a->box_rgb[v/8][1] = g; a->box_rgb[v/8][2] = b;
    a->box_part[v/8] = bone_of_box[a->boxes];
    a->boxes++;
}

/* Bones: 0 root, 1 torso, 2 head, 3/4 arms, 5/6 legs (with knees). */
static void actor_init(a3_actor *a, const unsigned char kit[3][3]) {
    memset(a, 0, sizeof *a);
    a->bone_count = 9;
    /* rest-space pivots, metres, feet at y=0 */
    const float piv[9][3] = {
        {0.00f, 0.92f, 0},   /* 0 root (hips) */
        {0.00f, 1.02f, 0},   /* 1 torso */
        {0.00f, 1.52f, 0},   /* 2 head */
        {0.22f, 1.42f, 0},   /* 3 left arm */
        {-0.22f, 1.42f, 0},  /* 4 right arm */
        {0.11f, 0.88f, 0},   /* 5 left thigh */
        {-0.11f, 0.88f, 0},  /* 6 right thigh */
        {0.11f, 0.48f, 0},   /* 7 left knee */
        {-0.11f, 0.48f, 0}   /* 8 right knee */
    };
    static const int parent[9] = { -1, 0, 1, 1, 1, 0, 0, 5, 6 };
    for (int i = 0; i < 9; i++) {
        a->bones[i].parent = parent[i];
        memcpy(a->bones[i].pivot, piv[i], sizeof piv[i]);
        xf_identity(&a->bones[i].pose);
    }
    /* geometry in rest space */
    add_box(a, 0, 1.22f, 0, 0.17f, 0.28f, 0.11f, kit[0][0], kit[0][1], kit[0][2]); /* torso */
    add_box(a, 0, 1.62f, 0, 0.10f, 0.11f, 0.10f, 200, 160, 130);                  /* head  */
    add_box(a, 0.26f, 1.18f, 0, 0.07f, 0.26f, 0.07f, 200, 160, 130);              /* l arm */
    add_box(a, -0.26f, 1.18f, 0, 0.07f, 0.26f, 0.07f, 200, 160, 130);             /* r arm */
    /* thigh in the shorts colour, shin in the sock colour: one box per segment
     * so the knee can bend and the kit still reads at sprite size. */
    add_box(a, 0.11f, 0.68f, 0, 0.085f, 0.20f, 0.085f, kit[1][0], kit[1][1], kit[1][2]);
    add_box(a, -0.11f, 0.68f, 0, 0.085f, 0.20f, 0.085f, kit[1][0], kit[1][1], kit[1][2]);
    add_box(a, 0.11f, 0.30f, 0, 0.070f, 0.18f, 0.070f, kit[2][0], kit[2][1], kit[2][2]);
    add_box(a, -0.11f, 0.30f, 0, 0.070f, 0.18f, 0.070f, kit[2][0], kit[2][1], kit[2][2]);
    add_box(a, 0.11f, 0.045f, 0.03f, 0.075f, 0.045f, 0.11f, 22, 24, 30);  /* l boot */
    add_box(a, -0.11f, 0.045f, 0.03f, 0.075f, 0.045f, 0.11f, 22, 24, 30); /* r boot */
}

/* Run cycle. phase in [0,1): legs alternate, arms counter-swing, torso bobs. */
/* Rotation about a joint: T(p) * R * T(-p). Without this the mesh is
 * rotated about the world origin and every limb flies off its socket. */
static void xf_about(a3_xform *out, const a3_xform *rot, const float pivot[3]) {
    a3_xform plus, minus;
    xf_identity(&plus);
    xf_identity(&minus);
    for (int i = 0; i < 3; i++) { plus.t[i] = pivot[i]; minus.t[i] = -pivot[i]; }
    a3_xform tmp;
    xf_mul(&tmp, rot, &minus);
    xf_mul(out, &plus, &tmp);
}

/* Run cycle. phase in [0,1): legs alternate, arms counter-swing, torso bobs. */
static void actor_pose(a3_actor *a, float phase, float facing) {
    const float tau = 6.28318530718f;
    const float sw  = sinf(phase * tau);
    const float sw2 = sinf(phase * tau + 3.14159265f);
    a3_xform rot, local;

    for (int i = 0; i < a->bone_count; i++) xf_identity(&a->bones[i].pose);

    /* facing rotates the whole rig; it belongs on the root. */
    xf_rot_y(&rot, facing);
    xf_about(&a->bones[0].pose, &rot, a->bones[0].pivot);
    a->bones[0].pose.t[1] += 0.020f * cosf(phase * 2 * tau);

    xf_rot_x(&rot, 0.10f);              /* slight forward lean */
    xf_about(&a->bones[1].pose, &rot, a->bones[1].pivot);
    xf_identity(&a->bones[2].pose);     /* head keeps the torso's tilt */
    xf_rot_x(&rot, -0.95f * sw);
    xf_about(&a->bones[3].pose, &rot, a->bones[3].pivot);
    xf_rot_x(&rot, -0.95f * sw2);
    xf_about(&a->bones[4].pose, &rot, a->bones[4].pivot);
    xf_rot_x(&rot,  0.90f * sw);
    xf_about(&a->bones[5].pose, &rot, a->bones[5].pivot);
    xf_rot_x(&rot,  0.90f * sw2);
    xf_about(&a->bones[6].pose, &rot, a->bones[6].pivot);

    /* The trailing leg folds at the knee; the leading one stays nearly straight.
     * Without this the run reads as two sticks scissoring. */
    float knee_l = sw  < 0.0f ? -1.25f * sw  : -0.15f * sw;
    float knee_r = sw2 < 0.0f ? -1.25f * sw2 : -0.15f * sw2;
    xf_rot_x(&rot, knee_l);
    xf_about(&a->bones[7].pose, &rot, a->bones[7].pivot);
    xf_rot_x(&rot, knee_r);
    xf_about(&a->bones[8].pose, &rot, a->bones[8].pivot);

    for (int i = 0; i < a->bone_count; i++) {
        int parent = a->bones[i].parent;
        if (parent < 0) a->world[i] = a->bones[i].pose;
        else            xf_mul(&a->world[i], &a->world[parent], &a->bones[i].pose);
    }
    (void)local;
}

/* ---- raster ----------------------------------------------------------- */

typedef struct { float z; unsigned char rgb[3]; unsigned char on; } a3_px;

/* Render into a caller buffer of A3_W*A3_H RGBA (alpha 0 = empty).
 * basis: camera right/up vectors in the actor's rest space, so the caller can
 * aim the camera without re-posing the mesh. */
void actor3d_render(a3_actor *a, float phase, float facing,
                    const float light[3], unsigned char *rgba) {
    static a3_px fb[A3_H][A3_W];
    for (int y = 0; y < A3_H; y++)
        for (int x = 0; x < A3_W; x++) { fb[y][x].on = 0; fb[y][x].z = 1e9f; }

    actor_pose(a, phase, facing);

    /* transform every box vertex by its bone */
    float world[80][3];
    for (int b = 0; b < a->boxes; b++) {
        int part = a->box_part[b];
        for (int i = 0; i < 8; i++)
            xf_point(&a->world[part], a->verts[b*8+i], world[b*8+i]);
    }

    /* camera: orthographic along +z, actor 0..1.75 m maps to A3_H */
    const float scale = (A3_H - 4) / 1.80f;
    const float cx = A3_W * 0.5f;
    const float cy = A3_H - 2.0f;

    static const int faces[12][3] = {
        {0,1,2},{0,2,3}, {5,4,7},{5,7,6}, {4,0,3},{4,3,7},
        {1,5,6},{1,6,2}, {3,2,6},{3,6,7}, {4,5,1},{4,1,0}
    };

    for (int b = 0; b < a->boxes; b++) {
        for (int f = 0; f < 12; f++) {
            float p[3][3];
            for (int k = 0; k < 3; k++)
                memcpy(p[k], world[b*8 + faces[f][k]], sizeof p[k]);
            float e1[3] = { p[1][0]-p[0][0], p[1][1]-p[0][1], p[1][2]-p[0][2] };
            float e2[3] = { p[2][0]-p[0][0], p[2][1]-p[0][1], p[2][2]-p[0][2] };
            float n[3] = { e1[1]*e2[2]-e1[2]*e2[1],
                           e1[2]*e2[0]-e1[0]*e2[2],
                           e1[0]*e2[1]-e1[1]*e2[0] };
            float len = sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
            if (len < 1e-6f) continue;
            for (int i = 0; i < 3; i++) n[i] /= len;
            if (n[2] > 0.0f) continue;                  /* back face */

            float sx[3], sy[3];
            for (int k = 0; k < 3; k++) {
                sx[k] = cx + p[k][0] * scale;
                sy[k] = cy - p[k][1] * scale;
            }
            float minx = sx[0], maxx = sx[0], miny = sy[0], maxy = sy[0];
            for (int k = 1; k < 3; k++) {
                if (sx[k] < minx) minx = sx[k];
                if (sx[k] > maxx) maxx = sx[k];
                if (sy[k] < miny) miny = sy[k];
                if (sy[k] > maxy) maxy = sy[k];
            }
            if (maxx < 0 || minx >= A3_W || maxy < 0 || miny >= A3_H) continue;

            float area = (sx[1]-sx[0])*(sy[2]-sy[0]) - (sx[2]-sx[0])*(sy[1]-sy[0]);
            if (fabsf(area) < 1e-6f) continue;

            float dif = 0.55f + 0.45f * (n[0]*light[0] + n[1]*light[1] + n[2]*light[2]);
            if (dif > 1.0f) dif = 1.0f;
            if (dif < 0.0f) dif = 0.0f;
            unsigned char col[3];
            for (int i = 0; i < 3; i++) {
                float v = a->box_rgb[b][i] * dif + 26.0f * (1.0f - dif);
                col[i] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
            }

            int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx);
            int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > A3_W-1) x1 = A3_W-1;
            if (y1 > A3_H-1) y1 = A3_H-1;

            for (int y = y0; y <= y1; y++) {
                for (int x = x0; x <= x1; x++) {
                    float px = x + 0.5f, py = y + 0.5f;
                    float w0 = (sx[1]-px)*(sy[2]-py) - (sx[2]-px)*(sy[1]-py);
                    float w1 = (sx[2]-px)*(sy[0]-py) - (sx[0]-px)*(sy[2]-py);
                    float w2 = (sx[0]-px)*(sy[1]-py) - (sx[1]-px)*(sy[0]-py);
                    if ((w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                        (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                        float z = (p[0][2] + p[1][2] + p[2][2]) / 3.0f;
                        if (z < fb[y][x].z || !fb[y][x].on) {
                            fb[y][x].z = z;
                            fb[y][x].on = 1;
                            memcpy(fb[y][x].rgb, col, 3);
                        }
                    }
                }
            }
        }
    }

    for (int y = 0; y < A3_H; y++) {
        for (int x = 0; x < A3_W; x++) {
            unsigned char *o = rgba + (y * A3_W + x) * 4;
            if (fb[y][x].on) {
                o[0] = fb[y][x].rgb[0]; o[1] = fb[y][x].rgb[1];
                o[2] = fb[y][x].rgb[2]; o[3] = 255;
            } else {
                o[0] = o[1] = o[2] = o[3] = 0;
            }
        }
    }
}

int actor3d_width(void) { return A3_W; }
int actor3d_height(void) { return A3_H; }

/* ---- standalone test -------------------------------------------------- */

#ifdef ACTOR3D_TEST
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "actor3d.ppm";
    const int cols = argc > 2 ? atoi(argv[2]) : 6;
    const int rows = argc > 3 ? atoi(argv[3]) : 3;

    static const unsigned char kit[3][3] = {
        { 32, 74, 168 },    /* shirt */
        { 236, 240, 246 },  /* shorts, white */
        { 32, 74, 168 }     /* socks, blue: must contrast with the shorts or the
                             * knee bend is invisible at this resolution */
    };
    static a3_actor actor;
    static unsigned char frame[A3_W * A3_H * 4];
    actor_init(&actor, kit);

    const int W = A3_W * cols, H = A3_H * rows;
    unsigned char *img = calloc((size_t)W * H * 3, 1);
    if (!img) return 1;

    for (int r = 0; r < rows; r++) {
        float facing = (float)r * (6.28318530718f / (float)rows);
        for (int c = 0; c < cols; c++) {
            float phase = (float)c / (float)cols;
            float light[3] = { -0.42f, 0.62f, -0.66f };
            actor3d_render(&actor, phase, facing, light, frame);
            for (int y = 0; y < A3_H; y++) {
                for (int x = 0; x < A3_W; x++) {
                    const unsigned char *s = frame + (y * A3_W + x) * 4;
                    unsigned char *d = img + (((r*A3_H + y) * W) + (c*A3_W + x)) * 3;
                    if (s[3]) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
                    else { d[0] = 16; d[1] = 22; d[2] = 30; }
                }
            }
        }
    }

    FILE *f = fopen(out, "wb");
    if (!f) return 1;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    fwrite(img, 1, (size_t)W * H * 3, f);
    fclose(f);
    printf("%s %dx%d (%d direcoes x %d fases)\n", out, W, H, rows, cols);
    free(img);
    return 0;
}
#endif
