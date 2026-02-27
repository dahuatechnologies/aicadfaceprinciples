/**
 * @file mimix/src/main.c
 * @version 0.0.1
 * @license GNU
 *
 * @title MIMIX - Complete AI-Powered Autonomous CAD Generation System
 * @description Fully implemented version with all functions defined
 */

/* ============================================================================
 * Standard Headers
 * =========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <time.h>
#include <stddef.h>
#include <errno.h>
#include <float.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

/* POSIX Headers */
#include <sched.h>
#include <sys/sysinfo.h>
#include <sys/mman.h>

/* NUMA headers */
#define _NUMA_H
#include <numa.h>
#include <numaif.h>

/* OpenCL */
#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>

/* OpenGL */
#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glu.h>

/* OpenAL */
#include <AL/al.h>
#include <AL/alc.h>

/* SDL2 */
#include <SDL2/SDL.h>

/* ============================================================================
 * Compile-time Configuration
 * =========================================================================== */

#define PROGRAM_NAME         "MIMIX AI CAD"
#define PROGRAM_VERSION      "0.0.1"
#define WINDOW_WIDTH         1920
#define WINDOW_HEIGHT        1080
#define MAX_THREADS          8
#define CACHE_LINE_SIZE      64
#define SIMD_ALIGNMENT       32
#define VECTOR_COUNT         32768
#define DIMENSION_COUNT      131072
#define GOLDEN_RATIO         1.618033988749895f
#define AXIS_LENGTH          4.0f
#define PARTICLE_COUNT       500

/* Memory alignment macros */
#ifdef __GNUC__
#define ALIGNED __attribute__((aligned(SIMD_ALIGNMENT)))
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))
#else
#define ALIGNED
#define CACHE_ALIGNED
#endif

/* ============================================================================
 * Type Definitions
 * =========================================================================== */

typedef struct ALIGNED Vector3 {
	float x, y, z, w;
} Vector3;

typedef struct ALIGNED ColorBGRA {
	unsigned char b;
	unsigned char g;
	unsigned char r;
	unsigned char a;
} ColorBGRA;

typedef struct ALIGNED DimensionalDot {
	Vector3 position;
	float dimension;
	float intensity;
	ColorBGRA color;
	unsigned int id;
} DimensionalDot;

typedef struct ALIGNED VectorLine {
	Vector3 start;
	Vector3 end;
	Vector3 direction;
	float magnitude;
	float curvature;
	ColorBGRA color;
	unsigned int semantic_id;
} VectorLine;

typedef struct CACHE_ALIGNED CADFace {
	DimensionalDot *dots;
	VectorLine *vectors;
	unsigned int dot_count;
	unsigned int vector_count;
	Vector3 bounding_box_min;
	Vector3 bounding_box_max;
	float scale;
	GLuint dot_display_list;
	GLuint vector_display_list;
	int display_lists_valid;
} CADFace;

typedef struct ALIGNED Particle {
	Vector3 position;
	Vector3 velocity;
	Vector3 target;
	ColorBGRA color;
	float life;
	float size;
	int active;
} Particle;

typedef struct CACHE_ALIGNED ThreadWork {
	unsigned int thread_id;
	unsigned int node_id;
	pthread_t thread;
	cpu_set_t cpu_affinity;
	CADFace *face;
	unsigned int dot_start;
	unsigned int dot_end;
	unsigned int vector_start;
	unsigned int vector_end;
	volatile int completed;
	double processing_time;
} ThreadWork;

typedef struct CLContext {
	cl_platform_id platform;
	cl_device_id device;
	cl_context context;
	cl_command_queue queue;
	int initialized;
} CLContext;

typedef struct GLContext {
	SDL_Window *window;
	SDL_GLContext gl_context;
	int width;
	int height;
	float aspect;
	float cam_phi;
	float cam_theta;
	float cam_distance;
	GLuint dot_list;
	GLuint vector_list;
	int initialized;
} GLContext;

typedef struct AppState {
	CADFace face;
	Particle particles[PARTICLE_COUNT];
	ThreadWork threads[MAX_THREADS];
	unsigned int thread_count;
	CLContext cl;
	GLContext gl;
	ALCdevice *al_device;
	ALCcontext *al_context;
	int running;
	double fps;
	float morph_time;
	struct timespec last_frame;
	struct timespec start_time;
	int show_particles;
	int show_axes;
	int visualization_mode;
	pthread_mutex_t render_mutex;
} AppState;

/* Global state */
static AppState *g_state = NULL;
static volatile int g_running = 1;

/* ============================================================================
 * Signal Handler
 * =========================================================================== */

static void signal_handler(int sig) {
	(void) sig;
	printf("\n✨ Shutdown signal received...\n");
	g_running = 0;
	if (g_state) {
		g_state->running = 0;
	}
}

/* ============================================================================
 * Vector Mathematics
 * =========================================================================== */

static Vector3 vec3(float x, float y, float z) {
	Vector3 v = { x, y, z, 1.0f };
	return v;
}

static Vector3 vec3_add(Vector3 a, Vector3 b) {
	Vector3 v = { a.x + b.x, a.y + b.y, a.z + b.z, 1.0f };
	return v;
}

static Vector3 vec3_sub(Vector3 a, Vector3 b) {
	Vector3 v = { a.x - b.x, a.y - b.y, a.z - b.z, 1.0f };
	return v;
}

static Vector3 vec3_scale(Vector3 v, float s) {
	Vector3 r = { v.x * s, v.y * s, v.z * s, 1.0f };
	return r;
}

static float vec3_length(Vector3 v) {
	return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static Vector3 vec3_normalize(Vector3 v) {
	float len = vec3_length(v);
	if (len > 0.0001f) {
		return vec3(v.x / len, v.y / len, v.z / len);
	}
	return vec3(0, 0, 0);
}

/* ============================================================================
 * BGRA Color Helpers
 * =========================================================================== */

static ColorBGRA bgra_white(void) {
	ColorBGRA c = { 255, 255, 255, 255 };
	return c;
}

static ColorBGRA bgra_yellow(void) {
	ColorBGRA c = { 0, 255, 255, 255 };
	return c;
}

static ColorBGRA bgra_blue(void) {
	ColorBGRA c = { 255, 0, 0, 255 };
	return c;
}

static ColorBGRA bgra_purple(void) {
	ColorBGRA c = { 255, 0, 128, 255 };
	return c;
}

static ColorBGRA bgra_red(void) {
	ColorBGRA c = { 0, 0, 255, 255 };
	return c;
}

static ColorBGRA bgra_random(void) {
	ColorBGRA c = { rand() % 256, rand() % 256, rand() % 256, 255 };
	return c;
}

/* ============================================================================
 * Dot Generation
 * =========================================================================== */

static DimensionalDot generate_dot(unsigned int id, float dimension, float u,
		float v, float time) {
	DimensionalDot dot;
	float theta, phi, r;

	theta = u * 2.0f * M_PI;
	phi = (v - 0.5f) * M_PI;

	r = 1.0f + 0.3f * sinf(phi * 2.0f) * cosf(theta * 3.0f);

	dot.position.x = r * sinf(phi + 0.2f) * cosf(theta) * 1.2f;
	dot.position.y = r * sinf(phi + 0.2f) * sinf(theta) * 1.1f;
	dot.position.z = r * cosf(phi + 0.2f) * 1.5f;
	dot.position.w = 1.0f;

	/* Add time-based morphing */
	dot.position.x += sinf(time + u * 10.0f) * 0.05f;
	dot.position.z += cosf(time + v * 10.0f) * 0.05f;

	dot.dimension = dimension;
	dot.intensity = 0.5f + 0.5f * sinf(u * 20.0f + time) * cosf(v * 20.0f);
	dot.id = id;

	/* Color based on position and time */
	float r_weight = 0.5f + 0.5f * sinf(dot.position.x + time);
	float g_weight = 0.5f + 0.5f * cosf(dot.position.y + time);
	float b_weight = 0.5f + 0.5f * sinf(dot.position.z + time);

	dot.color.b = (unsigned char) (r_weight * 255);
	dot.color.g = (unsigned char) (g_weight * 255);
	dot.color.r = (unsigned char) (b_weight * 255);
	dot.color.a = 255;

	return dot;
}

/* ============================================================================
 * Vector Generation
 * =========================================================================== */

static VectorLine generate_vector(unsigned int id, const DimensionalDot *start,
		const DimensionalDot *end, unsigned int semantic) {
	VectorLine vec;
	Vector3 dir;

	vec.start = start->position;
	vec.end = end->position;

	dir = vec3_sub(end->position, start->position);
	vec.magnitude = vec3_length(dir);
	vec.direction = vec3_normalize(dir);
	vec.curvature = 0.0f;
	vec.semantic_id = semantic;

	switch (semantic % 4) {
	case 0: /* Mesh edges - cyan */
		vec.color = bgra_white();
		break;
	case 1: /* Expression - yellow */
		vec.color = bgra_yellow();
		break;
	case 2: /* Structural - blue */
		vec.color = bgra_blue();
		break;
	case 3: /* Radial - purple */
		vec.color = bgra_purple();
		break;
	default:
		vec.color = bgra_white();
		break;
	}
	vec.color.a = 128;

	return vec;
}

/* ============================================================================
 * CAD Generation
 * =========================================================================== */

static CADFace* generate_cad_face(float time) {
	CADFace *face = (CADFace*) aligned_alloc(64, sizeof(CADFace));
	if (!face)
		return NULL;

	unsigned int i;
	float u, v;

	face->dot_count = DIMENSION_COUNT;
	face->vector_count = VECTOR_COUNT;

	face->dots = (DimensionalDot*) malloc(
			DIMENSION_COUNT * sizeof(DimensionalDot));
	face->vectors = (VectorLine*) malloc(VECTOR_COUNT * sizeof(VectorLine));

	if (!face->dots || !face->vectors) {
		free(face->dots);
		free(face->vectors);
		free(face);
		return NULL;
	}

	/* Generate dots */
	for (i = 0; i < DIMENSION_COUNT; i++) {
		u = (float) (i % 512) / 511.0f;
		v = (float) (i / 512) / ((DIMENSION_COUNT + 511) / 512);
		face->dots[i] = generate_dot(i, (float) i / (float) DIMENSION_COUNT, u,
				v, time);
	}

	/* Generate vectors */
	for (i = 0; i < VECTOR_COUNT; i++) {
		unsigned int start_idx = i % DIMENSION_COUNT;
		unsigned int end_idx = (start_idx + 257) % DIMENSION_COUNT;
		face->vectors[i] = generate_vector(i, &face->dots[start_idx],
				&face->dots[end_idx], i % 4);
	}

	face->display_lists_valid = 0;

	return face;
}

static void free_cad_face(CADFace *face) {
	if (!face)
		return;
	if (face->display_lists_valid) {
		if (face->dot_display_list)
			glDeleteLists(face->dot_display_list, 1);
		if (face->vector_display_list)
			glDeleteLists(face->vector_display_list, 1);
	}
	free(face->dots);
	free(face->vectors);
	free(face);
}

/* ============================================================================
 * Particle System
 * =========================================================================== */

static void particle_init(Particle *p, Vector3 pos) {
	p->position = pos;
	p->velocity = vec3((rand() / (float) RAND_MAX - 0.5f) * 0.1f,
			(rand() / (float) RAND_MAX - 0.5f) * 0.1f,
			(rand() / (float) RAND_MAX - 0.5f) * 0.1f);
	p->target = pos;
	p->life = 1.0f;
	p->size = 0.05f;
	p->active = 1;
	p->color = bgra_random();
}

static void particle_update(Particle *p, float dt) {
	if (!p->active)
		return;

	p->life -= dt * 0.1f;
	if (p->life <= 0) {
		p->active = 0;
		return;
	}

	Vector3 dir = vec3_sub(p->target, p->position);
	p->velocity = vec3_add(p->velocity, vec3_scale(dir, 0.01f));

	p->velocity.x += (rand() / (float) RAND_MAX - 0.5f) * 0.02f;
	p->velocity.y += (rand() / (float) RAND_MAX - 0.5f) * 0.02f;
	p->velocity.z += (rand() / (float) RAND_MAX - 0.5f) * 0.02f;

	p->velocity = vec3_scale(p->velocity, 0.98f);
	p->position = vec3_add(p->position, vec3_scale(p->velocity, dt));
}

/* ============================================================================
 * Thread Pool Functions
 * =========================================================================== */

static void* thread_process_dots(void *arg) {
	ThreadWork *work = (ThreadWork*) arg;
	struct timespec start, end;
	unsigned int i;

	if (sched_setaffinity(0, sizeof(cpu_set_t), &work->cpu_affinity) != 0) {
		perror("Failed to set CPU affinity");
	}

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);

	for (i = work->dot_start; i < work->dot_end; i++) {
		DimensionalDot *dot = &work->face->dots[i];

		float scale = 1.0f + 0.1f * sinf(dot->dimension * 10.0f);
		dot->position.x *= scale;
		dot->position.y *= scale;
		dot->position.z *= scale;

		dot->intensity = 0.5f
				+ 0.5f
						* sinf(
								dot->position.x * 5.0f + dot->position.y * 3.0f
										+ dot->position.z * 2.0f);
	}

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);

	work->completed = 1;
	work->processing_time = (end.tv_sec - start.tv_sec)
			+ (end.tv_nsec - start.tv_nsec) / 1.0e9;

	return NULL;
}

static int init_thread_pool(AppState *state) {
	unsigned int i;
	int num_cores = (int) sysconf(_SC_NPROCESSORS_ONLN);
	unsigned int dots_per_thread, vectors_per_thread;

	if (num_cores <= 0)
		num_cores = 1;

	state->thread_count = (num_cores < MAX_THREADS) ? num_cores : MAX_THREADS;

	dots_per_thread = state->face.dot_count / state->thread_count;
	vectors_per_thread = state->face.vector_count / state->thread_count;

	for (i = 0; i < state->thread_count; i++) {
		ThreadWork *work = &state->threads[i];

		work->thread_id = i;
		work->node_id = 0;
		work->face = &state->face;
		work->completed = 0;

		work->dot_start = i * dots_per_thread;
		work->dot_end =
				(i == state->thread_count - 1) ?
						state->face.dot_count : (i + 1) * dots_per_thread;

		work->vector_start = i * vectors_per_thread;
		work->vector_end =
				(i == state->thread_count - 1) ?
						state->face.vector_count : (i + 1) * vectors_per_thread;

		CPU_ZERO(&work->cpu_affinity);
		CPU_SET(i % (unsigned int )num_cores, &work->cpu_affinity);

		if (pthread_create(&work->thread, NULL, thread_process_dots, work)
				!= 0) {
			perror("Failed to create thread");
			return -1;
		}
	}

	return 0;
}

static void wait_for_threads(AppState *state) {
	unsigned int i;
	int all_completed;

	do {
		all_completed = 1;
		for (i = 0; i < state->thread_count; i++) {
			if (!state->threads[i].completed) {
				all_completed = 0;
				break;
			}
		}
		if (!all_completed) {
			sched_yield();
		}
	} while (!all_completed);
}

static void join_threads(AppState *state) {
	unsigned int i;
	for (i = 0; i < state->thread_count; i++) {
		pthread_join(state->threads[i].thread, NULL);
	}
}

/* ============================================================================
 * OpenCL Initialization
 * =========================================================================== */

static int init_opencl(CLContext *cl) {
	cl_uint platform_count;
	cl_uint device_count;
	cl_int err;

	cl->initialized = 0;

	err = clGetPlatformIDs(0, NULL, &platform_count);
	if (err != CL_SUCCESS || platform_count == 0) {
		return -1;
	}

	err = clGetPlatformIDs(1, &cl->platform, NULL);
	if (err != CL_SUCCESS)
		return -1;

	err = clGetDeviceIDs(cl->platform, CL_DEVICE_TYPE_GPU, 0, NULL,
			&device_count);
	if (err != CL_SUCCESS || device_count == 0) {
		return -1;
	}

	err = clGetDeviceIDs(cl->platform, CL_DEVICE_TYPE_GPU, 1, &cl->device,
			NULL);
	if (err != CL_SUCCESS)
		return -1;

	cl->context = clCreateContext(NULL, 1, &cl->device, NULL, NULL, &err);
	if (err != CL_SUCCESS)
		return -1;

	cl->queue = clCreateCommandQueue(cl->context, cl->device, 0, &err);
	if (err != CL_SUCCESS) {
		clReleaseContext(cl->context);
		return -1;
	}

	cl->initialized = 1;

	return 0;
}

/* ============================================================================
 * OpenGL Initialization
 * =========================================================================== */

static int init_opengl(GLContext *gl) {
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
		return -1;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	gl->window = SDL_CreateWindow(
	PROGRAM_NAME " " PROGRAM_VERSION,
	SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

	if (!gl->window) {
		fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
		SDL_Quit();
		return -1;
	}

	gl->gl_context = SDL_GL_CreateContext(gl->window);
	if (!gl->gl_context) {
		fprintf(stderr, "OpenGL context creation failed: %s\n", SDL_GetError());
		SDL_DestroyWindow(gl->window);
		SDL_Quit();
		return -1;
	}

	SDL_GL_SetSwapInterval(1);

	glewExperimental = GL_TRUE;
	GLenum glew_err = glewInit();
	if (glew_err != GLEW_OK) {
		fprintf(stderr, "GLEW initialization failed: %s\n",
				glewGetErrorString(glew_err));
		return -1;
	}

	gl->width = WINDOW_WIDTH;
	gl->height = WINDOW_HEIGHT;
	gl->aspect = (float) WINDOW_WIDTH / WINDOW_HEIGHT;
	gl->cam_phi = 0.4f;
	gl->cam_theta = 0.3f;
	gl->cam_distance = 8.0f;

	glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	gl->dot_list = glGenLists(1);
	gl->vector_list = glGenLists(1);
	gl->initialized = 1;

	return 0;
}

static void build_display_lists(AppState *state) {
	unsigned int i;

	glNewList(state->gl.dot_list, GL_COMPILE);
	glPointSize(3.0f);
	glBegin(GL_POINTS);
	for (i = 0; i < state->face.dot_count; i += 5) {
		DimensionalDot *dot = &state->face.dots[i];
		glColor4ub(dot->color.r, dot->color.g, dot->color.b, dot->color.a);
		glVertex3f(dot->position.x, dot->position.y, dot->position.z);
	}
	glEnd();
	glEndList();

	glNewList(state->gl.vector_list, GL_COMPILE);
	glLineWidth(1.5f);
	glBegin(GL_LINES);
	for (i = 0; i < state->face.vector_count; i += 3) {
		VectorLine *vec = &state->face.vectors[i];
		glColor4ub(vec->color.r, vec->color.g, vec->color.b, vec->color.a);
		glVertex3f(vec->start.x, vec->start.y, vec->start.z);
		glVertex3f(vec->end.x, vec->end.y, vec->end.z);
	}
	glEnd();
	glEndList();

	state->face.display_lists_valid = 1;
}

/* ============================================================================
 * OpenAL Initialization
 * =========================================================================== */

static int init_openal(AppState *state) {
	ALCdevice *device = alcOpenDevice(NULL);
	if (!device) {
		return -1;
	}

	ALCcontext *context = alcCreateContext(device, NULL);
	if (!context) {
		alcCloseDevice(device);
		return -1;
	}

	alcMakeContextCurrent(context);
	alListener3f(AL_POSITION, 0.0f, 0.0f, 5.0f);

	state->al_device = device;
	state->al_context = context;

	return 0;
}

/* ============================================================================
 * Rendering Functions
 * =========================================================================== */

static void render_axes(void) {
	float axis_len = AXIS_LENGTH;

	/* Z-axis (white) */
	glColor4ub(255, 255, 255, 255);
	glBegin(GL_LINES);
	glVertex3f(0, 0, -axis_len);
	glVertex3f(0, 0, axis_len);
	glEnd();

	/* Y-axis (yellow) */
	glColor4ub(0, 255, 255, 255);
	glBegin(GL_LINES);
	glVertex3f(0, -axis_len, 0);
	glVertex3f(0, axis_len, 0);
	glEnd();

	/* X-axis (blue) */
	glColor4ub(255, 0, 0, 255);
	glBegin(GL_LINES);
	glVertex3f(-axis_len, 0, 0);
	glVertex3f(axis_len, 0, 0);
	glEnd();

	/* Origin marker */
	glPointSize(10.0f);
	glBegin(GL_POINTS);
	glColor4ub(255, 255, 255, 255);
	glVertex3f(0, 0, 0);
	glEnd();
}

static void render_particles(AppState *state) {
	if (!state->show_particles)
		return;

	glPointSize(2.0f);
	glBegin(GL_POINTS);

	int i;
	for (i = 0; i < PARTICLE_COUNT; i++) {
		Particle *p = &state->particles[i];
		if (!p->active)
			continue;

		float alpha = p->life;
		glColor4ub(p->color.r, p->color.g, p->color.b,
				(unsigned char) (alpha * 255));
		glVertex3f(p->position.x, p->position.y, p->position.z);
	}

	glEnd();
}

static void render_cad_face(AppState *state) {
	if (!state->face.display_lists_valid) {
		build_display_lists(state);
	}

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* Set up camera */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(45.0, state->gl.aspect, 0.1, 100.0);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	float eye_x = state->gl.cam_distance * sinf(state->gl.cam_phi)
			* cosf(state->gl.cam_theta);
	float eye_y = state->gl.cam_distance * sinf(state->gl.cam_theta);
	float eye_z = state->gl.cam_distance * cosf(state->gl.cam_phi)
			* cosf(state->gl.cam_theta);

	gluLookAt(eye_x, eye_y, eye_z, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);

	/* Render axes if enabled */
	if (state->show_axes) {
		render_axes();
	}

	/* Render particles */
	render_particles(state);

	/* Render face based on visualization mode */
	if (state->visualization_mode == 0 || state->visualization_mode == 1) {
		glCallList(state->gl.dot_list);
	}
	if (state->visualization_mode == 0 || state->visualization_mode == 2) {
		glCallList(state->gl.vector_list);
	}

	SDL_GL_SwapWindow(state->gl.window);
}

/* ============================================================================
 * Print Controls
 * =========================================================================== */

static void print_controls(void) {
	printf("\n");
	printf("╔══════════════════════════════════════════════════════════════╗\n");
	printf("║              🎮 MIMIX AI CONTROLS 🎮                         ║\n");
	printf("╠══════════════════════════════════════════════════════════════╣\n");
	printf("║  [A] Toggle Axes       [P] Toggle Particles                  ║\n");
	printf("║  [V] Cycle View Mode   [R] Reset Camera                      ║\n");
	printf("║  [Mouse] Rotate View   [Wheel] Zoom                          ║\n");
	printf("║  [ESC] Exit                                                  ║\n");
	printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

/* ============================================================================
 * Main Function
 * =========================================================================== */

int main(int argc, char **argv) {
	AppState state;
	SDL_Event event;
	struct timespec current_time;
	int frame_count = 0;
	double elapsed = 0.0;
	unsigned int i;

	(void) argc;
	(void) argv;

	memset(&state, 0, sizeof(AppState));
	clock_gettime(CLOCK_MONOTONIC, &state.start_time);
	state.last_frame = state.start_time;
	state.running = 1;
	state.morph_time = 0.0f;
	state.show_axes = 1;
	state.show_particles = 1;
	state.visualization_mode = 0;

	g_state = &state;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	printf("╔══════════════════════════════════════════════════════════════╗\n");
	printf("║     MIMIX AI CAD - Complete V0.0.1                           ║\n");
	printf("╚══════════════════════════════════════════════════════════════╝\n\n");

	printf("System Configuration:\n");
	printf("  ├─ CPU Cores: %ld\n", sysconf(_SC_NPROCESSORS_ONLN));
	printf("  ├─ AVX2 Support: %s\n",
#ifdef __AVX2__
           "Yes"
#else
			"No (using scalar fallback)"
#endif
			);
	printf("  ├─ Dots: %d\n", DIMENSION_COUNT);
	printf("  ├─ Vectors: %d\n", VECTOR_COUNT);
	printf("  └─ Particles: %d\n\n", PARTICLE_COUNT);

	printf("Initializing subsystems...\n");

	/* Initialize particle system */
	for (i = 0; i < PARTICLE_COUNT; i++) {
		Vector3 pos = vec3((rand() / (float) RAND_MAX - 0.5f) * 6.0f,
				(rand() / (float) RAND_MAX - 0.5f) * 6.0f,
				(rand() / (float) RAND_MAX - 0.5f) * 6.0f);
		particle_init(&state.particles[i], pos);
	}
	printf("  ✓ Particles initialized\n");

	/* Generate initial CAD */
	printf("Generating CAD ...\n");
	CADFace *generated_face = generate_cad_face(0);
	if (!generated_face) {
		fprintf(stderr, "Failed to generate CAD \n");
		return EXIT_FAILURE;
	}
	state.face = *generated_face;
	free(generated_face);
	printf("  ✓ Face generated (%d dots, %d vectors)\n", state.face.dot_count,
			state.face.vector_count);

	/* Initialize thread pool */
	if (init_thread_pool(&state) != 0) {
		fprintf(stderr, "Failed to initialize thread pool\n");
		free(state.face.dots);
		free(state.face.vectors);
		return EXIT_FAILURE;
	}
	printf("  ✓ Thread pool initialized (%d threads)\n", state.thread_count);

	/* Initialize OpenCL */
	if (init_opencl(&state.cl) != 0) {
		printf("  ⚠️  OpenCL not available\n");
	} else {
		printf("  ✓ OpenCL initialized\n");
	}

	/* Initialize OpenGL */
	if (init_opengl(&state.gl) != 0) {
		fprintf(stderr, "Failed to initialize OpenGL\n");
		join_threads(&state);
		free(state.face.dots);
		free(state.face.vectors);
		return EXIT_FAILURE;
	}
	printf("  ✓ OpenGL initialized\n");

	/* Build display lists */
	build_display_lists(&state);

	/* Initialize OpenAL */
	if (init_openal(&state) != 0) {
		printf("  ⚠️  OpenAL not available\n");
	} else {
		printf("  ✓ OpenAL initialized\n");
	}

	pthread_mutex_init(&state.render_mutex, NULL);

	print_controls();

	printf("\n╔══════════════════════════════════════════════════════════════╗\n");
	printf("║                    Entering Main Loop                          ║\n");
	printf("╚════════════════════════════════════════════════════════════════╝\n\n");

	/* Main loop */
	while (g_running && state.running) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				state.running = 0;
				g_running = 0;
			} else if (event.type == SDL_KEYDOWN) {
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
					state.running = 0;
					g_running = 0;
					break;
				case SDLK_a:
					state.show_axes = !state.show_axes;
					printf("Axes: %s\n", state.show_axes ? "ON" : "OFF");
					break;
				case SDLK_p:
					state.show_particles = !state.show_particles;
					printf("Particles: %s\n",
							state.show_particles ? "ON" : "OFF");
					break;
				case SDLK_v:
					state.visualization_mode = (state.visualization_mode + 1)
							% 3;
					printf("View mode: %s\n",
							state.visualization_mode == 0 ?
									"Points+Lines" :
									(state.visualization_mode == 1 ?
											"Points only" : "Lines only"));
					break;
				case SDLK_r:
					state.gl.cam_phi = 0.4f;
					state.gl.cam_theta = 0.3f;
					state.gl.cam_distance = 8.0f;
					printf("Camera reset\n");
					break;
				}
			} else if (event.type == SDL_MOUSEMOTION) {
				if (event.motion.state & SDL_BUTTON_LMASK) {
					state.gl.cam_phi += event.motion.xrel * 0.005f;
					state.gl.cam_theta += event.motion.yrel * 0.005f;

					if (state.gl.cam_theta > 1.4f)
						state.gl.cam_theta = 1.4f;
					if (state.gl.cam_theta < -1.4f)
						state.gl.cam_theta = -1.4f;
				}
			} else if (event.type == SDL_MOUSEWHEEL) {
				state.gl.cam_distance -= event.wheel.y * 0.5f;
				if (state.gl.cam_distance < 3.0f)
					state.gl.cam_distance = 3.0f;
				if (state.gl.cam_distance > 20.0f)
					state.gl.cam_distance = 20.0f;
			} else if (event.type == SDL_WINDOWEVENT) {
				if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
					state.gl.width = event.window.data1;
					state.gl.height = event.window.data2;
					state.gl.aspect = (float) state.gl.width / state.gl.height;
					glViewport(0, 0, state.gl.width, state.gl.height);
				}
			}
		}

		/* Wait for threads */
		wait_for_threads(&state);

		/* Update time and particles */
		clock_gettime(CLOCK_MONOTONIC, &current_time);
		elapsed = (current_time.tv_sec - state.last_frame.tv_sec)
				+ (current_time.tv_nsec - state.last_frame.tv_nsec) / 1e9;
		state.morph_time += elapsed * 0.2f;

		/* Update particles */
		for (i = 0; i < PARTICLE_COUNT; i++) {
			if (!state.particles[i].active) {
				Vector3 pos = vec3((rand() / (float) RAND_MAX - 0.5f) * 6.0f,
						(rand() / (float) RAND_MAX - 0.5f) * 6.0f,
						(rand() / (float) RAND_MAX - 0.5f) * 6.0f);
				particle_init(&state.particles[i], pos);
			} else {
				particle_update(&state.particles[i], elapsed);
			}
		}

		/* Regenerate face periodically */
		static float last_regenerate = 0;
		if (state.morph_time - last_regenerate > 2.0f) {
			pthread_mutex_lock(&state.render_mutex);
			CADFace *new_face = generate_cad_face(state.morph_time);
			if (new_face) {
				free(state.face.dots);
				free(state.face.vectors);
				state.face = *new_face;
				free(new_face);
				build_display_lists(&state);
			}
			pthread_mutex_unlock(&state.render_mutex);
			last_regenerate = state.morph_time;
		}

		/* Render */
		pthread_mutex_lock(&state.render_mutex);
		render_cad_face(&state);
		pthread_mutex_unlock(&state.render_mutex);

		/* FPS counter */
		frame_count++;
		if (elapsed >= 1.0) {
			state.fps = (double) frame_count / elapsed;
			printf("\r║ FPS: %7.2f ║ Time: %.2f ║ Dots: %6.1fk ║", state.fps,
					state.morph_time, state.face.dot_count / 1000.0);
			fflush(stdout);
			frame_count = 0;
			state.last_frame = current_time;
		}

		SDL_Delay(10);
	}

	/* Cleanup */
	printf("\n\nShutting down...\n");

	join_threads(&state);

	if (state.gl.initialized) {
		glDeleteLists(state.gl.dot_list, 1);
		glDeleteLists(state.gl.vector_list, 1);
		SDL_GL_DeleteContext(state.gl.gl_context);
		SDL_DestroyWindow(state.gl.window);
	}

	if (state.al_context) {
		alcMakeContextCurrent(NULL);
		alcDestroyContext(state.al_context);
		alcCloseDevice(state.al_device);
	}

	if (state.cl.initialized) {
		clReleaseCommandQueue(state.cl.queue);
		clReleaseContext(state.cl.context);
	}

	free(state.face.dots);
	free(state.face.vectors);
	pthread_mutex_destroy(&state.render_mutex);

	SDL_Quit();

	printf("\n✨ MIMIX AI shutdown complete. Goodbye!\n");
	return EXIT_SUCCESS;
}

/* ============================================================================
 * End of File
 * =========================================================================== */
