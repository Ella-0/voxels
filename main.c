#include <stdbool.h>
#include <stdint.h>

#include <stdlib.h>
#include <stdio.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include "simplex.h"

typedef struct voxel_t {
	int32_t x, y, z;
	uint32_t col;
	int32_t scale_x, scale_y, scale_z;
	uint32_t id;
} voxel_t;

typedef struct chunk_t {
	voxel_t voxels[64][64][64];
	voxel_t to_draw[64 * 64 * 64];
	size_t to_draw_count;
	unsigned int cube_ebo;
	unsigned int cube_vbo;
	unsigned int vao;
	unsigned int off_vbo;
	size_t lod;
} chunk_t;

typedef struct app_t {
	GLFWwindow *window;
	size_t cube_count;
	chunk_t chunks[8][8][8];
} app_t;

void chunk_gen(chunk_t *self, int32_t x, int32_t y, int32_t z) {
	struct osn_context *ons;
	struct osn_context *ons_r;
	struct osn_context *ons_g;
	struct osn_context *ons_b;
	open_simplex_noise(0, &ons);
	open_simplex_noise(1, &ons_r);
	open_simplex_noise(2, &ons_g);
	open_simplex_noise(3, &ons_b);

	for (int32_t i = 0; i < 64; i++) {
		for (int32_t j = 0; j < 64; j++) {
			for (int32_t k = 0; k < 64; k++) {
				int32_t off_x = i + x * 64;
				int32_t off_y = j + y * 64;
				int32_t off_z = k + z * 64;
				self->voxels[i][j][k].x = off_x;
				self->voxels[i][j][k].y = off_y;
				self->voxels[i][j][k].z = off_z;
				self->voxels[i][j][k].col = open_simplex_noise3(ons, (off_x) / 32.0, (off_y) / 32.0, (off_z) / 32.0)>0?0xff:0x00;
				self->voxels[i][j][k].col |= 0x01000000 * ((uint8_t)((open_simplex_noise3(ons_r, (off_x) / 32.0, (off_y) / 32.0, (off_z) / 32.0) + 1) * 128.0));
				self->voxels[i][j][k].col |= 0x00010000 * ((uint8_t)((open_simplex_noise3(ons_g, (off_x) / 32.0, (off_y) / 32.0, (off_z) / 32.0) + 1) * 128.0));
				self->voxels[i][j][k].col |= 0x00000100 * ((uint8_t)((open_simplex_noise3(ons_b, (off_x) / 32.0, (off_y) / 32.0, (off_z) / 32.0) + 1) * 128.0));
				self->voxels[i][j][k].scale_x = 1;
				self->voxels[i][j][k].scale_y = 1;
				self->voxels[i][j][k].scale_z = 1;
			}
		}
	}

	/* greedy 1d */

	int32_t greedy_x, greedy_y, greedy_z;

	bool eating = false;

	self->to_draw_count = 0;

	voxel_t tmp_pool[64][64][64];
	size_t counts[64][64];

	for (int32_t i = 0; i < 64; i++) {
		for (int32_t j = 0; j < 64; j++) {
			for (int32_t k = 0; k < 64; k++) {
				uint32_t me = self->voxels[i][j][k].col&0xff;
				if (!eating && me) {
					greedy_x = i;
					greedy_y = j;
					greedy_z = k;
					eating = true;
				} else if (eating && !me) {
					eating = false;
					self->to_draw[self->to_draw_count] = self->voxels[greedy_x][greedy_y][greedy_z];
					self->to_draw[self->to_draw_count].scale_z = k - greedy_z;
					self->to_draw_count++;
				}
			}
			if (eating) {
				eating = false;
				self->to_draw[self->to_draw_count] = self->voxels[greedy_x][greedy_y][greedy_z];
				self->to_draw[self->to_draw_count].scale_z = 64 - greedy_z;
				self->to_draw_count++;
			}
		}
	}

	/* greedy 2d */

	for (int32_t i = 0; i < (self->to_draw_count - 1); i++) {
		voxel_t *a = self->to_draw+i;
		voxel_t *b = self->to_draw+i+1;
		while (a->scale_z == b->scale_z && a->z == b->z && a->x == b->x) {
			a->scale_y++;
			self->to_draw_count--;
			*b = self->to_draw[self->to_draw_count];
			b++;
			i++;
		}
	}

	static size_t draw_count = 0;
	draw_count += self->to_draw_count;

	fprintf(stderr, "draw count: %lu\n", draw_count);

	glGenVertexArrays(1, &self->vao);
	glBindVertexArray(self->vao);

	unsigned int buffers[3] = { 0 };
	glGenBuffers(3, buffers);

	self->cube_ebo = buffers[0];
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self->cube_ebo);

	static const unsigned int elements[] = {
		3, 2, 6, 7, 4, 2, 0,
		3, 1, 6, 5, 4, 1, 0
	};

	glNamedBufferData(self->cube_ebo, sizeof(elements), elements, GL_STATIC_DRAW);

	self->cube_vbo = buffers[1];
	glBindBuffer(GL_ARRAY_BUFFER, self->cube_vbo);

	static const float cube_data[] = {
		1.0, 1.0, 1.0,
		0.0, 1.0, 1.0,
		1.0, 1.0, 0.0,
		0.0, 1.0, 0.0,
		1.0, 0.0, 1.0,
		0.0, 0.0, 1.0,
		0.0, 0.0, 0.0,
		1.0, 0.0, 0.0
	};

	glNamedBufferData(self->cube_vbo, sizeof(cube_data), cube_data, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), NULL);
	glEnableVertexAttribArray(0);

	self->off_vbo = buffers[2];
	glBindBuffer(GL_ARRAY_BUFFER, self->off_vbo);
	glNamedBufferData(self->off_vbo, sizeof(voxel_t) * self->to_draw_count, self->to_draw, GL_STATIC_DRAW);

	glVertexAttribIPointer(1, 4, GL_INT, sizeof(voxel_t), NULL);
	glVertexAttribIPointer(2, 4, GL_INT, sizeof(voxel_t), (void*)(sizeof(int32_t) * 4));
	glVertexAttribDivisor(1, 1);
	glVertexAttribDivisor(2, 1);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);

	self->lod = 0;
}

void chunk_set_lod(chunk_t *self, size_t lod) {
	glBindVertexArray(self->vao);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self->cube_ebo);
	glBindBuffer(GL_ARRAY_BUFFER, self->cube_vbo);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), NULL);
	glEnableVertexAttribArray(0);

	glBindBuffer(GL_ARRAY_BUFFER, self->off_vbo);

	glVertexAttribIPointer(1, 4, GL_INT, (4 * sizeof(int32_t)) << lod, NULL);
	glVertexAttribDivisor(1, 1);
	glEnableVertexAttribArray(1);

	self->lod = lod;
}

void chunk_draw(chunk_t const *self) {
	glBindVertexArray(self->vao);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self->cube_ebo);

	glDrawElementsInstanced(GL_TRIANGLE_STRIP, 14, GL_UNSIGNED_INT, NULL, self->to_draw_count);
}

char const *vs_src = ""
"#version 460 core\n"
"layout (location = 0) in vec3 in_pos;"
"layout (location = 1) in ivec4 offset;"
"layout (location = 2) in ivec4 scale;"
"layout (location = 0) out vec4 out_col;"
"mat4 p = mat4("
	"1.42814, 0, 0, 0,"
	"0, 1.42814, 0, 0,"
	"0, 0, -0.9998, -1,"
	"0, 0, -0.2, 0"
");"
"layout (location = 0) uniform mat4 v;"
"void main() {"
	"gl_Position =  p * v * vec4((in_pos * scale.xyz + offset.xyz) * vec3(0.1), 1.0);"
	"out_col = vec4(in_pos, 0.0);"
	"out_col.w = ((offset.w >>  0) & 0xff) / 256.0;"
"}";

char const * fs_src = ""
"#version 460 core\n"
"layout (location = 0) in vec4 in_col;"
"layout (location = 0) out vec4 out_col;"
"void main() {"
	"out_col = vec4(in_col.xyz, 1.0);"
"}";

void debug_callback(
	unsigned int source, unsigned int type, unsigned int id,
	unsigned int severity,
	int len, char const *msg,
	void const *user_param
) {
	fprintf(stderr, "opengl: %s\n", msg);
}

void app_setup(app_t *self) {
	if (!glfwInit()) {
		fprintf(stderr, "glfw: init failed\n");
		exit(1);
	}

	glfwDefaultWindowHints();
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	self->window = glfwCreateWindow(640, 640, "voxels", NULL, NULL);

	if (!self->window) {
		fprintf(stderr, "glfw: create window failed\n");
		exit(1);
	}

	glfwMakeContextCurrent(self->window);

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(&debug_callback, NULL);

	glEnable(GL_DEPTH_TEST);

	glEnable(GL_CULL_FACE);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	for (int32_t i = 0; i < 4; i++)
		for(int32_t j = 0; j < 4; j++)
			for(int32_t k = 0; k < 4; k++) {
		chunk_gen(self->chunks[i][j]+k, i, j, k);
	}

	unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &vs_src, NULL);
	glCompileShader(vs);

	unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &fs_src, NULL);
	glCompileShader(fs);

	unsigned int prog = glCreateProgram();;
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	glUseProgram(prog);
}

bool app_loop(app_t *self) {
	static double time = 0.0f;
	static uint64_t frame = 0;
	static bool update_view = true;

	static float v_mat[] = {
		0.1, 0.0, 0.0, 0.0,
		0.0, 0.1, 0.0, 0.0,
		0.0, 0.0, 0.1, 0.0,
		0.0, 0.0, 0.0, 1.0
	};

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	for (int32_t i = 0; i < 4; i++)
		for(int32_t j = 0; j < 4; j++)
			for(int32_t k = 0; k < 4; k++) {
		chunk_draw(self->chunks[i][j]+k);
	}

	glfwSwapBuffers(self->window);
	glfwPollEvents();

	if (glfwGetKey(self->window, GLFW_KEY_W) != GLFW_RELEASE) {
		v_mat[4 * 2 + 3] += 0.01;
		update_view = true;
	}

	if (glfwGetKey(self->window, GLFW_KEY_A) != GLFW_RELEASE) {
		v_mat[4 * 0 + 3] += 0.01;
		update_view = true;
	}

	if (glfwGetKey(self->window, GLFW_KEY_S) != GLFW_RELEASE) {
		v_mat[4 * 2 + 3] -= 0.01;
		update_view = true;
	}

	if (glfwGetKey(self->window, GLFW_KEY_D) != GLFW_RELEASE) {
		v_mat[4 * 0 + 3] -= 0.01;
		update_view = true;
	}

	if (glfwGetKey(self->window, GLFW_KEY_SPACE) != GLFW_RELEASE) {
		v_mat[4 * 1 + 3] -= 0.01;
		update_view = true;
	}

	if (glfwGetKey(self->window, GLFW_KEY_LEFT_SHIFT) != GLFW_RELEASE) {
		v_mat[4 * 1 + 3] += 0.01;
		update_view = true;
	}

	if (update_view) {
		glUniformMatrix4fv(0, 1, true, v_mat);
		update_view = false;
	}

	if (frame == 32) {
		double ntime = glfwGetTime();
		double dur = ntime - time;
		double fps = frame / dur;
		printf("fps: %f\n", fps);
		time = ntime;
		frame = 0;
	}
	frame++;
	return !glfwWindowShouldClose(self->window);
}

int main() {
	app_t *app = malloc(sizeof(app_t));
	for (app_setup(app);app_loop(app););
}
