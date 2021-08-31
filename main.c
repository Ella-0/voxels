#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include <string.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include "simplex.h"
#include "voct.h"

#define MAX_TO_DRAW (128*128*64)

typedef struct chunk_t {
	voxel_cache_t cache;
	voct_node_t tree;
	voxel_t to_draw[MAX_TO_DRAW];
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

typedef struct chunk_thread_t {
	chunk_t *chunk;
	int32_t x, y, z;
} chunk_thread_t;

void chunk_gen(chunk_t *self, int32_t x, int32_t y, int32_t z) {
	voxel_cache_new(&self->cache);
	self->tree.depth = 8;
	self->tree.is_leaf = false;
	memset(self->tree.children, 0, sizeof(self->tree.children));

	struct osn_context *simplex;
	open_simplex_noise(1, &simplex);

	size_t voxel_count = 0;

	for (int32_t i = 0; i < 128; i++) {
		for (int32_t j = 0; j < 128; j++) {
			for (int32_t k = 0; k < 128; k++) {
				int32_t off_x = i;
				int32_t off_y = j;
				int32_t off_z = k;
				if (open_simplex_noise3(simplex, off_x / 32.0f, off_y / 32.0f, off_z / 32.0f) > 0)
					voxel_set(&self->cache, &self->tree, &self->tree, off_x, off_y, off_z), voxel_count++;
			}
		}
	}

	// voxel_set_visible(&self->tree, &self->tree);

	// voxel_greedy(&self->tree);

	for (size_t i = 0; i < VOXEL_CACHE_SIZE && self->to_draw_count < MAX_TO_DRAW; i++) {
		block_flags_t flags = self->cache.ptr[i].scale & 0xff;
		if (flags & BLOCK_FLAG_EXISTS && !(flags & BLOCK_FLAG_HIDDEN)) {
			self->to_draw[self->to_draw_count] = self->cache.ptr[i];
			self->to_draw_count++;
		}
	}

	// dump_tree(&self->tree);

	fprintf(stderr, "voxel_count: %lu\n", voxel_count);
	fprintf(stderr, "to_draw_count: %lu\n", self->to_draw_count);
	fprintf(stderr, "ring_pos: %lu\n", self->cache.ring_index);

	self->lod = 0;
}

void chunk_load(chunk_t *self) {
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
	//glNamedBufferData(self->off_vbo, sizeof(voxel_t) * VOXEL_CACHE_SIZE, self->cache.ptr, GL_STATIC_DRAW);

	glVertexAttribIPointer(1, 4, GL_INT, sizeof(voxel_t), NULL);
	glVertexAttribDivisor(1, 1);
	glEnableVertexAttribArray(1);
}

void *chunk_thread(void *ptr) {
	chunk_thread_t *thread_info = (chunk_thread_t *) ptr;
	fprintf(stderr, "generating %d %d %d\n", thread_info->x, thread_info->y, thread_info->z);
	chunk_gen(thread_info->chunk, thread_info->x, thread_info->y, thread_info->z);
	pthread_exit(NULL);
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
	//glDrawElementsInstanced(GL_TRIANGLE_STRIP, 14, GL_UNSIGNED_INT, NULL, 64 * 64 * 64);
}

char const *vs_src = ""
"#version 460 core\n"
"layout (location = 0) in vec3 in_pos;"
"layout (location = 1) in ivec4 offset;"
"layout (location = 0) out vec4 out_col;"
"mat4 p = mat4("
	"1.42814, 0, 0, 0,"
	"0, 1.42814, 0, 0,"
	"0, 0, -0.9998, -1,"
	"0, 0, -0.2, 0"
");"
"layout (location = 0) uniform mat4 v;"
"void main() {"
	"uvec3 scale;"
	"scale.x = 1 << (offset.w >> 24 & 0xff);"
	"scale.y = 1 << (offset.w >> 16 & 0xff);"
	"scale.z = 1 << (offset.w >>  8 & 0xff);"
	"gl_Position =  p * v * vec4((in_pos * scale + offset.xyz) * vec3(0.1), 1.0);"
	"out_col = vec4(in_pos, 0.0);"
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

	pthread_t threads[8][8][8];
	chunk_thread_t thread_infos[8][8][8];

	for (int32_t i = 0; i < 1; i++)
		for(int32_t j = 0; j < 1; j++)
			for(int32_t k = 0; k < 1; k++) {
		thread_infos[i][j][k] = (chunk_thread_t){
			.chunk = self->chunks[i][j]+k,
			.x = i,
			.y = j,
			.z = k
		};
		pthread_create(threads[i][j]+k, NULL, chunk_thread, thread_infos[i][j]+k);
	}

	for (int32_t i = 0; i < 1; i++)
		for(int32_t j = 0; j < 1; j++)
			for(int32_t k = 0; k < 1; k++) {
		pthread_join(threads[i][j][k], NULL);
		chunk_load(self->chunks[i][j]+k);
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

	for (int32_t i = 0; i < 1; i++)
		for(int32_t j = 0; j < 1; j++)
			for(int32_t k = 0; k < 1; k++) {
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
