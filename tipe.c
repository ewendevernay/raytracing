#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <inttypes.h>
#include <stdint.h>
#include <math.h>

#define max(x, y) (((x) < (y)) ? (y) : (x))

#pragma pack(push, 1)

typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BITMAPFILEHEADER;

typedef struct {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BITMAPINFOHEADER;

#pragma pack(pop)

typedef unsigned int uint;
typedef unsigned short int ushort;
typedef unsigned char uchar;

struct object {
	void *shape;
	void *material_data;
	int (*collision)(float *w, float *n, void *shape, float *t);
	void (*material)(void *data, void *shape, float *n, float *point, uchar *dest);
};

static struct object scene[32];
static uint scene_count;

struct sphere {
	float radius;
	float center[3];
};

struct plane {
	float point[3];
	float normal[3];
};

struct disk {
	float point[3];
	float normal[3];
	float radius;
};

struct texture {
	int width;
	int height;
	float real_width;
	uchar *data;
};

struct disk_material_data {
	uchar color[3];
	float border;
    float n;
};

#define HEADER_SIZE 1024

void saveBMP(const char *filename, uint8_t *rgbData, int width, int height) {
    // Open file for writing in binary mode
    FILE *f = fopen(filename, "wb");
    if (!f) {
        printf("Error: Unable to open file %s for writing.\n", filename);
        return;
    }

    // BMP header definitions
    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER infoHeader;

    int rowSize = (width * 3 + 3) & (~3); // Row size, padded to the nearest 4 bytes
    int imageSize = rowSize * height; // Size of the pixel data

    // Fill the file header
    fileHeader.bfType = 0x4D42; // 'BM'
    fileHeader.bfSize = 14 + 40 + imageSize; // File size = headers + pixel data
    fileHeader.bfReserved1 = 0;
    fileHeader.bfReserved2 = 0;
    fileHeader.bfOffBits = 14 + 40; // Start of pixel data after headers

    // Fill the info header
    infoHeader.biSize = 40;
    infoHeader.biWidth = width;
    infoHeader.biHeight = height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 24; // 24-bit RGB (3 bytes per pixel)
    infoHeader.biCompression = 0; // No compression (BI_RGB)
    infoHeader.biSizeImage = imageSize;
    infoHeader.biXPelsPerMeter = 2835; // 72 DPI
    infoHeader.biYPelsPerMeter = 2835; // 72 DPI
    infoHeader.biClrUsed = 0;
    infoHeader.biClrImportant = 0;

    // Write the file header
    fwrite(&fileHeader, sizeof(BITMAPFILEHEADER), 1, f);

    // Write the info header
    fwrite(&infoHeader, sizeof(BITMAPINFOHEADER), 1, f);

    // Write the pixel data (bottom-to-top, BGR format with padding)
    uint8_t padding[3] = {0, 0, 0}; // Padding for row alignment
    for (int y = height - 1; y >= 0; y--) { // BMP rows are stored bottom-up
        fwrite(&rgbData[y * width * 3], 3, width, f); // Write the RGB data for each row
        fwrite(padding, 1, rowSize - width * 3, f);   // Write padding for alignment
    }

    // Close the file
    fclose(f);
    printf("BMP file saved successfully.\n");
}

uint8_t* readBMP(const char *filename, int *width, int *height) {
    // Open the BMP file for reading in binary mode
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("Error: Unable to open file %s for reading.\n", filename);
        return NULL;
    }

    // Read the BMP file header
    BITMAPFILEHEADER fileHeader;
    fread(&fileHeader, sizeof(BITMAPFILEHEADER), 1, f);

    // Verify that it is a BMP file by checking the file signature
    if (fileHeader.bfType != 0x4D42) {
        printf("Error: File is not a BMP file.\n");
        fclose(f);
        return NULL;
    }

    // Read the BMP info header
    BITMAPINFOHEADER infoHeader;
    fread(&infoHeader, sizeof(BITMAPINFOHEADER), 1, f);

    // Ensure the BMP is 24-bit uncompressed
    if (infoHeader.biBitCount != 24 || infoHeader.biCompression != 0) {
        printf("Error: Only uncompressed 24-bit BMP files are supported.\n");
        fclose(f);
        return NULL;
    }

    // Set width and height based on the BMP info header
    *width = infoHeader.biWidth;
    *height = infoHeader.biHeight;

    // Calculate the row size with padding (4-byte alignment)
    int rowSize = (*width * 3 + 3) & (~3);

    // Allocate memory for the RGB data
    uint8_t *rgbData = (uint8_t *)malloc((*width) * (*height) * 3);
    if (!rgbData) {
        printf("Error: Unable to allocate memory for RGB data.\n");
        fclose(f);
        return NULL;
    }

    // Move the file pointer to the beginning of the pixel data
    fseek(f, fileHeader.bfOffBits, SEEK_SET);

    // Read the pixel data from bottom to top
    for (int y = *height - 1; y >= 0; y--) {
        // Read one row of pixel data into the RGB array
        fread(&rgbData[y * (*width) * 3], 3, *width, f);

        // Skip any padding bytes at the end of each row
        fseek(f, rowSize - (*width) * 3, SEEK_CUR);
    }

    // Close the file
    fclose(f);

    printf("BMP file read successfully.\n");
    return rgbData;
}

static void get_color_at(float *w, float *n, uchar *dest, void *excluded);

inline static float dot(float *a, float *b) {
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline static void cpy(float *a, float *b) {
	a[0] = b[0];
	a[1] = b[1];
	a[2] = b[2];
}

inline static void sub(float *a, float *b) {
	a[0] -= b[0];
	a[1] -= b[1];
	a[2] -= b[2];
}

inline static void add(float *a, float *b) {
	a[0] += b[0];
	a[1] += b[1];
	a[2] += b[2];
}

inline static float norm(float *a) {
	return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

inline static void scale(float l, float *a) {
	a[0] *= l;
	a[1] *= l;
	a[2] *= l;
}

inline static void normalize(float *a) {
	float n = norm(a);
  	a[0] /= n;
	a[1] /= n;
	a[2] /= n;
}

static void vapply(float *a, float (*fn)(float)) {
	a[0] = fn(a[0]);
	a[1] = fn(a[1]);
	a[2] = fn(a[2]);
}

inline static void vectorial(float *a, float *b, float *out) {
  	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

inline static void lerp(float *a, float *b, float p) {
	a[0] = a[0] * (1.f - p) + b[0] * p;
	a[1] = a[1] * (1.f - p) + b[1] * p;
	a[2] = a[2] * (1.f - p) + b[2] * p;
}

inline static void rot_x(float *a, float theta) {
	float t[3];
	cpy(t, a);
	a[1] = t[1] * cosf(theta) - t[2] * sinf(theta);
	a[2] = t[1] * sinf(theta) + t[2] * cosf(theta);
}

inline static void rot_y(float *a, float theta) {
	float t[3];
	cpy(t, a);
	a[0] = t[0] * cosf(theta) + t[2] * sinf(theta);
	a[2] = t[0] * -sinf(theta) + t[2] * cosf(theta);
}

static int ray_sphere_collision_roots(float *w, float *n, struct sphere *sphere, float *t1, float *t2) {
	float wa[3], delta, b, c, r, nsq;

	cpy(wa, w);
	r = sphere->radius;
	sub(wa, sphere->center);
	nsq = dot(n, n);

	b = dot(wa, n);
	c = dot(wa, wa) - r * r;

	delta = b * b - c * nsq;

	if (delta < 0.f)
		return 0;

	delta = sqrtf(delta);
	*t1 = (-b - delta) / nsq;
	*t2 = (-b + delta) / nsq;
	return 1;
}

static int ray_sphere_collision(float *w, float *n, void *ptr, float *t) {
	float t1, t2;
	if (!ray_sphere_collision_roots(w, n, ptr, &t1, &t2))
		return 0;

	if (t1 < 0.f) {
		if (t2 > 0.f) {
			*t = t2;
			return 1;
		}
		return 0;
	}

	*t = t1;
	return 1;
}

static int ray_plane_collision(float *w, float *n, void *ptr, float *t) {
	struct plane *plane = ptr;
	float vw[3];

	cpy(vw, plane->point);
	sub(vw, w);
	*t = dot(vw, plane->normal) / dot(n, plane->normal);
	if (*t < 0.f)
		return 0;
	else
		return 1;
}

static int ray_disk_collision(float *w, float *n, void *ptr, float *t) {
	struct disk *disk = ptr;
	struct plane eq_plane;
	float dist[3], to_add[3];

	cpy(eq_plane.point, disk->point);
	cpy(eq_plane.normal, disk->normal);
	if (!ray_plane_collision(w, n, &eq_plane, t))
		return 0;

	cpy(dist, w);
	cpy(to_add, n);
	scale(*t, to_add);
	add(dist, to_add);
	sub(dist, disk->point);

	if (dot(dist, dist) <= disk->radius * disk->radius)
		return 1;
	else
		return 0;
}

static void basic_material(void *a, void *ptr, float *f, float *pos, uchar *dest) {
	memcpy(dest, a, 3);
}

static void rainbow_material_sphere(void *a, void *ptr, float *f, float *pos, uchar *dest) {
	struct sphere *sphere = ptr;
	float center_dist[3];

	cpy(center_dist, pos);
	sub(center_dist, sphere->center);
	vapply(center_dist, fabsf);
	scale(1.f / sphere->radius, center_dist);

	dest[0] = (uchar)(center_dist[0] * 255.f);
	dest[1] = (uchar)(center_dist[1] * 255.f);
	dest[2] = (uchar)(center_dist[2] * 255.f);
}

static void wierd_material_sphere(void *a, void *ptr, float *f, float *pos, uchar *dest) {
	struct sphere *sphere = ptr;
	float center_dist[3], abs_cd[3];

	cpy(center_dist, pos);
	sub(center_dist, sphere->center);
	scale(1.f / sphere->radius, center_dist);
	cpy(abs_cd, center_dist);
	vapply(abs_cd, fabsf);

	uchar colors[][3] = {
		{255, 0, 0},
		{0, 123, 56},
		{255, 255, 255},/* {198, 12, 170}, */
		{0, 4, 125}
	};

	int i = (int)(max((center_dist[0] + 1.f) * 2.f, 0.f)); // PROBLEMOS

	dest[0] = (uchar)((float)colors[i][0] * abs_cd[2]);
	dest[1] = (uchar)((float)colors[i][1] * abs_cd[2]);
	dest[2] = (uchar)((float)colors[i][2] * abs_cd[2]);
}

static float absolute_base1[3] = { 1.f, 0.f, 0.f },
	absolute_base2[3] = { 0.f, 1.f, 0.f },
	absolute_base3[3] = { 0.f, 0.f, 1.f };

static int is_colinear(float *a, float *b) {
	float prod[3], res;
	vectorial(a, b, prod);
	res = fabsf(prod[0]) + fabsf(prod[1]) + fabsf(prod[2]);

	if (res < 0.001f)
		return 1;
	else
		return 0;
}

static void get_basis_from_normal(float *n, float *v1, float *v2) {
	float proj[3];
	float n_sq;

	n_sq = dot(n, n);

	if (is_colinear(n, absolute_base1)) {
		if (is_colinear(n, absolute_base1)) {
			cpy(v1, absolute_base3);
		} else {
			cpy(v1, absolute_base2);
		}
	} else {
		cpy(v1, absolute_base1);
	}

	cpy(proj, n);
	scale(dot(v1, n) / n_sq, proj);
	sub(v1, proj);

	vectorial(n, v1, v2);

	normalize(v1);
	normalize(v2);
}

static void texture_material_plane(void *a, void *ptr, float *f, float *pos, uchar *dest) {
	struct plane *plane = ptr;
	struct texture *texture = a;
	float v1[3], v2[3], dist[3];
	float real_height;
	float x, y, junk;
	int i, j;

	get_basis_from_normal(plane->normal, v1, v2);

	cpy(dist, pos);
	sub(dist, plane->point);

	real_height = (texture->real_width / (float)texture->width) * texture->height;
	x = modff((dot(dist, v1) / texture->real_width), &junk);
	y = modff((dot(dist, v2) / real_height), &junk);
	i = (int)fabsf(x * texture->width);
	j = (int)fabsf(y * texture->height);

	memcpy(dest, texture->data + (j * texture->width + i) * 3, 3);
}

static void texture_material_sphere(void *a, void *ptr, float *f, float *pos, uchar *dest) {
	struct sphere *sphere = ptr;
	struct texture *texture = a;
	int i, j;
	float dist[3], lat, lng;
	cpy(dist, pos);
	sub(dist, sphere->center);
	normalize(dist);

	if (fabsf(dist[1]) > 1.f) {
		if (signbit(dist[2]))
			dist[1] = -1.f;
		else
			dist[1] = 1.f;
	}

	lat = M_PI - acos(dist[1]);
	if (dist[1] != 0.f)
		lng = atan(dist[0] / dist[2]);
	else
		lng = 0.f;

	j = (int)((lat / M_PI) * (float)texture->height);
	i = (int)(fabsf((lng + M_PI / 2.f) / M_PI) * (float)texture->width);

	memcpy(dest, texture->data + (j * texture->width + i) * 3, 3);
}

static void sin_material_plane(void *color_ptr, void *ptr, float *f, float *pos, uchar *dest) {
	struct plane *plane = ptr;
	uchar *color = color_ptr;
	float dist_vec[3], dist;

	cpy(dist_vec, pos);
	sub(dist_vec, plane->point);
	dist = (fabsf(dist_vec[0]) + fabsf(dist_vec[1]) + fabsf(dist_vec[2])) / 3;//norm(dist_vec);//dist_vec[1];

	float x = fabsf(sinf(dist * 3.f));

	dest[0] = (uchar)(((float)color[0]) * x);
	dest[1] = (uchar)(((float)color[1]) * x);
	dest[2] = (uchar)(((float)color[2]) * x);
}

static void get_mirror_direction(float *normal, float *k, float *v) {
	float n[3];
	cpy(n, normal);

	float kn = dot(k, n);
	float n_mag_sq = dot(n, n);
	scale(2.f * kn / n_mag_sq, n);
	cpy(v, k);
	sub(v, n);
}

static void mirror_material_sphere(void* a, void *shape_ptr, float *k, float *pos, uchar *dest) {
	struct sphere *sphere = shape_ptr;

	float n[3], v[3];
	cpy(n, pos);
	sub(n, sphere->center);

	get_mirror_direction(n, k, v);
	get_color_at(pos, v, dest, shape_ptr);
}

static void mirror_material_plane(void* a, void *shape_ptr, float *k, float *pos, uchar *dest) {
  	struct plane *plane = shape_ptr;
	float v[3];

	get_mirror_direction(plane->normal, k, v);
	get_color_at(pos, v, dest, shape_ptr);
}

static void mirror_material_disk(void *a, void *shape_ptr, float *k, float *pos, uchar *dest) {
	struct disk *disk = shape_ptr;
	struct disk_material_data *data = a;
	float dist[3], d;

	cpy(dist, pos);
	sub(dist, disk->point);
	if (dot(dist, dist) >= (disk->radius - data->border) * (disk->radius - data->border)) {
		d = fabsf(sinf(norm(dist) * (15.f / data->border)));

		for (uint i = 0; i < 3; i++) {
			dest[i] = (uchar)(d * (float)data->color[i]);
		}
	} else {
		float v[3];
		get_mirror_direction(disk->normal, k, v);
		get_color_at(pos, v, dest, shape_ptr);
	}
}

static int get_refraction_direction(float *k, float *n, float n1, float n2, float *v, float *cos_theta) {
	float i[3], j[3], l, sin_theta_prime;
	cpy(j, k);
	cpy(i, n);
	cpy(v, n);

	l = dot(j, v) / dot(v, v);
	scale(l, v);
	sub(j, v);

	normalize(i);
	normalize(j);

	*cos_theta = (-dot(k, i) / norm(k));
	sin_theta_prime = sqrt(1.f - (*cos_theta) * (*cos_theta)) * (n1 / n2);

	if (sin_theta_prime >= 1.f) {
		return 0; // Reflexion
	}

	// Refraction
	cpy(v, j);
	scale(sin_theta_prime, v);
	scale(sqrtf(1.f - sin_theta_prime * sin_theta_prime), i);
	sub(v, i);
	return 1;
}

static void isotropic_get_color(void *shape_ptr, float *n, float *k, float *pos, float indice, uchar *dest) {
	float v[3], r0, r, t, cos_theta, n1, n2;
	if (dot(k, n) < 0.f) {
		n1 = 1.f;
		n2 = indice;
	} else {
		n1 = indice;
		n2 = 1.f;
		scale(-1.f, n);
	}

	if (!get_refraction_direction(k, n, n1, n2, v, &cos_theta)) {
		get_mirror_direction(n, k, v);
		get_color_at(pos, v, dest, shape_ptr);
	} else {
		uchar reflexion_color[3], refraction_color[3];
		float n_pos[3];

		cpy(n_pos, pos);
		normalize(n);
		scale(0.0001f, n);
		sub(n_pos, n);

		r0 = (n1 - n2) / (n1 + n2);
		r0 *= r0;

		t = (1.f - cos_theta);
		t = t * t * t * t * t;

		r = r0 + (1.f - r0) * t;

		if (r < 0.05f) {
			get_color_at(n_pos, v, dest, NULL);
		} else {
			get_color_at(n_pos, v, refraction_color, NULL);

			get_mirror_direction(n, k, v);
			get_color_at(n_pos, v, reflexion_color, shape_ptr);

			for (uint i = 0; i < 3; i++) {
				dest[i] = (uchar)(r * (float)reflexion_color[i] + (1 - r) * (float)refraction_color[i]);
			}
		}
	}
}

static void isotropic_material_sphere(void *a, void *shape_ptr, float *k, float *pos, uchar *dest) {
	struct sphere *sphere = shape_ptr;
	float n[3];

	cpy(n, pos);
	sub(n, sphere->center);

	isotropic_get_color(shape_ptr, n, k, pos, *(float*)a, dest);
}

static void isotropic_material_disk(void *a, void *shape_ptr, float *k, float *pos, uchar *dest) {
	struct disk *disk = shape_ptr;
	struct disk_material_data *data = a;
	float n[3], n1, n2, dist[3];

	cpy(dist, pos);
	sub(dist, disk->point);

	if (norm(dist) >= disk->radius - data->border) {
		memcpy(dest, data->color, 3 * sizeof(uchar));
		return;
	}

	cpy(n, disk->normal);
	isotropic_get_color(shape_ptr, n, k, pos, data->n, dest);
}

#define MAX_RENDER_DEPTH 32
static uint render_depth = 0;

static void get_color_at(float *w, float *n, uchar *dest, void *excluded) {
	int min_id = -1;
	float min_t = INFINITY;
	render_depth++;

	if (render_depth >= MAX_RENDER_DEPTH) {
		dest[0] = 32;
		dest[1] = 12;
		dest[2] = 23;
		render_depth--;
		return;
	}

	for (uint i = 0; i < scene_count; i++) {
		float t;

		if (scene[i].shape != excluded && scene[i].collision(w, n, scene[i].shape, &t)) {
			if (t < min_t) {
				min_id = i;
				min_t = t;
			}
		}
	}

	if (min_id >= 0) {
		float point[3];
		cpy(point, n);
		scale(min_t, point);
		add(point, w);

		scene[min_id].material(scene[min_id].material_data, scene[min_id].shape, n, point, dest);
	} else {
		dest[0] = 32;
		dest[1] = 12;
		dest[2] = 23;
	}

	render_depth--;
}

void render(uint width, uint height, float *camera_pos, float *camera_rot, float camera_width, float camera_depth, uchar *data) {
	float pixel_size = camera_width / (float)width;

	for (int y = 0; y < (int)height; y++) {
		for (int x = 0; x < (int)width; x++) {
			float ray_n[3];

			ray_n[0] = (float)(-(int)width / 2 + x) * pixel_size;
			ray_n[1] = (float)(-(int)height / 2 + y) * pixel_size;
			ray_n[2] = camera_depth;

			rot_x(ray_n, camera_rot[0]);
			rot_y(ray_n, camera_rot[1]);
			get_color_at(camera_pos, ray_n, &data[(y * width + x) * 3], NULL);
		}
	}
}

static struct sphere g_sphere1 = { 100.f, { 0.f, 5.f, -3.f } },
	g_sphere2 = { 2.5f, { 0.f, 0.f, 7.f }},
	g_sphere3 = { 1.7f, { -4.f, 0.5f, 20.f }},
	g_sphere4 = { 3.f, { 4.5f, -1.5f, 20.f }},
	g_sphere5 = { 1.f, { 0.f, -3.2f, 6.f }};

static struct plane g_plane1 = { { 0.f, 2.f, 20.f }, { 0.f, -1.f, 0.f } },
	g_plane2 = { { 0.f, 10.f,  15.f }, { 0.f, 0.f, 1.f } },
  	g_plane3 = { { 0.f, 10.f,  -10.f }, { 0.f, -0.1f, -1.f } };

static struct disk g_disk1 = { { 1.f, -1.f, 10.f }, { 0.f, 0.f, -1.f }, 1.f },
    g_disk2 = { { 1.f, -1.f, 14.f }, { 0.f, 0.f, 1.f }, 1.f };

/* Renders the refraction plus reflexion image */
int main() {
	int height = 740 * 4;
	int width = 1024 * 4;
	uchar *data = malloc(3 * height * width);
	float camera_pos[] = { 0.f, 0.f, -8.f }, camera_rot[] = { 0.0f, 0.f, 0.f };

	struct texture grass_tex, skybox_tex, globe_tex, football_tex;

	grass_tex.data = readBMP("grass.bmp", &grass_tex.width, &grass_tex.height);
	grass_tex.real_width = 10.f;

	skybox_tex.data = readBMP("sky.bmp", &skybox_tex.width, &skybox_tex.height);
	skybox_tex.real_width = 2.f;

	globe_tex.data = readBMP("globe.bmp", &globe_tex.width, &globe_tex.height);
	globe_tex.real_width = 2.f;

	football_tex.data = readBMP("football.bmp", &football_tex.width, &football_tex.height);
	football_tex.real_width = 2.f;

	scene_count = 0;

	scene[scene_count].shape = &g_plane1;
	scene[scene_count].collision = &ray_plane_collision;
	scene[scene_count].material = &texture_material_plane;
	scene[scene_count].material_data = &grass_tex;
	scene_count++;

	scene[scene_count].shape = &g_sphere1;
	scene[scene_count].collision = &ray_sphere_collision;
	scene[scene_count].material = &texture_material_sphere;
	scene[scene_count].material_data = &skybox_tex;
	scene_count++;

	float n = 1.3;
	scene[scene_count].shape = &g_sphere2;
	scene[scene_count].collision = &ray_sphere_collision;
	scene[scene_count].material = &isotropic_material_sphere;
	scene[scene_count].material_data = &n;
	scene_count++;

	scene[scene_count].shape = &g_sphere3;
	scene[scene_count].collision = &ray_sphere_collision;
	scene[scene_count].material = &texture_material_sphere;
	scene[scene_count].material_data = &football_tex;
	scene_count++;

	scene[scene_count].shape = &g_sphere4;
	scene[scene_count].collision = &ray_sphere_collision;
	scene[scene_count].material = &texture_material_sphere;
	scene[scene_count].material_data = &globe_tex;
	scene_count++;

	scene[scene_count].shape = &g_sphere5;
	scene[scene_count].collision = &ray_sphere_collision;
	scene[scene_count].material = &rainbow_material_sphere;
	scene_count++;

	render(width, height, camera_pos, camera_rot, 0.5f, 0.6f, data);

	saveBMP("image.bmp", data, width, height);
	return 0;
}
