#include "forces.h"
#include "sdl_wrapper.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CIRCLE_POINTS 40

#define MAX ((vector_t){.x = 80.0, .y = 80.0})

#define N_ROWS 11
#define ROW_SPACING 3.6
#define COL_SPACING 3.5
#define WALL_ANGLE atan2(ROW_SPACING, COL_SPACING / 2)
#define WALL_LENGTH hypot(MAX.x / 2, MAX.y)

#define PEG_RADIUS 0.5
#define BALL_RADIUS 1.0
#define DROP_INTERVAL 1 // s
#define PEG_ELASTICITY 0.3
#define BALL_ELASTICITY 0.7
#define WALL_WIDTH 1.0
#define DELTA_X 1.0
#define DROP_Y (MAX.y - 3.0)
#define START_VELOCITY ((vector_t){.x = 0.0, .y = -8.0})

#define BALL_MASS 2.0

#define BALL_COLOR ((rgb_color_t){1, 0, 0})
#define PEG_COLOR ((rgb_color_t){0, 1, 0})
#define WALL_COLOR ((rgb_color_t){0, 0, 1})

#define G 6.67E-11          // N m^2 / kg^2
#define M 6E24              // kg
#define g 9.8               // m / s^2
#define R (sqrt(G * M / g)) // m

typedef enum {
  BALL,
  FROZEN,
  WALLS, // or peg
  GRAVITY_PEG
} type_t;

type_t *make_type_info(type_t type) {
  type_t *info = malloc(sizeof(*info));
  *info = type;
  return info;
}

type_t get_type(body_t *body) {
  return *(type_t *)body_get_info(body);
}

/** Generates a random number between 0 and 1 */
double rand_double(void) { return (double)rand() / RAND_MAX; }

/** Computes the center of the peg in the given row and column */
vector_t get_peg_center(size_t row, size_t col) {
  vector_t center = {.x = MAX.x / 2 + (col - row * 0.5) * COL_SPACING,
                     .y = MAX.y - (row + 1) * ROW_SPACING};
  return center;
}

/** Creates an Earth-like mass to accelerate the balls */
void make_gravity_body(scene_t *scene) {
  // Will be offscreen, so shape is irrelevant
  list_t *gravity_ball = rect_init(1, 1);
  body_t *body = body_init_with_info(gravity_ball, M, WALL_COLOR,
                                     make_type_info(GRAVITY_PEG), free);

  // Move a distnace R below the scene
  vector_t gravity_center = {.x = MAX.x / 2, .y = -(sqrt(G * M / g))};
  body_set_centroid(body, gravity_center);
  scene_add_body(scene, body);
}

/** Creates a ball with the given starting position and velocity */
body_t *get_ball(vector_t center, vector_t velocity) {
  list_t *shape = circle_init(BALL_RADIUS, CIRCLE_POINTS);
  body_t *ball = body_init_with_info(shape, BALL_MASS, BALL_COLOR,
                                     make_type_info(BALL), free);

  body_set_centroid(ball, center);
  body_set_velocity(ball, velocity);

  return ball;
}

/** Collision handler to freeze a ball when it collides with a frozen body */
void freeze(body_t *ball, body_t *target, vector_t axis, void *aux) {
  // Skip body if it was already frozen
  if (body_is_removed(ball))
    return;

  // Replace the ball with a frozen version
  body_remove(ball);
  body_t *frozen = get_ball(body_get_centroid(ball), VEC_ZERO);
  *((type_t *)body_get_info(frozen)) = FROZEN;
  scene_t *scene = aux;
  scene_add_body(scene, frozen);

  // Make other falling bodies freeze when they collide with this body
  size_t body_count = scene_bodies(scene);
  for (size_t i = 0; i < body_count; i++) {
    body_t *body = scene_get_body(scene, i);
    if (get_type(body) == BALL) {
      create_collision(scene, body, frozen, freeze, scene, NULL);
    }
  }
}

/** Adds a ball to the scene */
void add_ball(scene_t *scene) {
  // Add the ball to the scene.
  vector_t ball_center = {.x = MAX.x / 2 + (rand_double() - 0.5) * DELTA_X,
                          .y = DROP_Y};
  body_t *ball = get_ball(ball_center, START_VELOCITY);
  size_t body_count = scene_bodies(scene);
  scene_add_body(scene, ball);

  // Add force creators with other bodies
  for (size_t i = 0; i < body_count; i++) {
    body_t *body = scene_get_body(scene, i);
    switch (get_type(body)) {
    case BALL:
      // Bounce off other balls
      create_physics_collision(scene, BALL_ELASTICITY, ball, body);
      break;
    case WALL:
      // Bounce off walls and pegs
      create_physics_collision(scene, PEG_ELASTICITY, ball, body);
      break;
    case FROZEN:
      // Freeze when hitting the ground or frozen balls
      create_collision(scene, ball, body, freeze, scene, NULL);
      break;
    case GRAVITY_PEG:
      // Simulate earth's gravity acting on the ball
      create_newtonian_gravity(scene, G, body, ball);
    }
  }
}

/** Adds the pegs to the scene */
void add_pegs(scene_t *scene) {
  // Add N_ROWS and N_COLS of pegs.
  for (size_t i = 1; i <= N_ROWS; i++) {
    for (size_t j = 0; j <= i; j++) {
      list_t *polygon = circle_init(PEG_RADIUS, CIRCLE_POINTS);
      body_t *body = body_init_with_info(polygon, INFINITY, PEG_COLOR,
                                         make_type_info(WALLS), free);
      body_set_centroid(body, get_peg_center(i, j));
      scene_add_body(scene, body);
    }
  }
}

/** Adds the walls to the scene */
void add_walls(scene_t *scene) {
  // Add walls
  list_t *rect = rect_init(WALL_LENGTH, WALL_WIDTH);
  body_t *body = body_init_with_info(rect, INFINITY, WALL_COLOR,
                                     make_type_info(WALLS), free);
  body_set_centroid(body, (vector_t){.x = WALL_LENGTH / 2, .y = 0.0});
  body_rotate_about(body, WALL_ANGLE, VEC_ZERO);
  scene_add_body(scene, body);

  rect = rect_init(WALL_LENGTH, WALL_WIDTH);
  body = body_init_with_info(rect, INFINITY, WALL_COLOR, make_type_info(WALLS),
                             free);
  body_set_centroid(body, (vector_t){.x = MAX.x - WALL_LENGTH / 2, .y = 0.0});
  body_rotate_about(body, -WALL_ANGLE, (vector_t){.x = MAX.x, .y = 0.0});
  scene_add_body(scene, body);

  // Ground is special; it freezes balls when they touch it
  rect = rect_init(MAX.x, WALL_WIDTH);
  body = body_init_with_info(rect, INFINITY, WALL_COLOR, make_type_info(FROZEN),
                             free);
  body_set_centroid(body, (vector_t){.x = MAX.x / 2, .y = WALL_WIDTH / 2});
  scene_add_body(scene, body);
}

typedef struct state {
  scene_t *scene;
  double time_since_drop;
} state_t;

state_t *emscripten_init(void) {
  srand(time(NULL));
  // Initialize scene
  sdl_init(VEC_ZERO, MAX);
  scene_t *scene = scene_init();
  // Add elements to the scene
  make_gravity_body(scene);
  add_pegs(scene);
  add_walls(scene);

  // Repeatedly render scene
  double time_since_drop = INFINITY;

  state_t *state = malloc(sizeof(state_t));
  state->scene = scene;
  state->time_since_drop = time_since_drop;
  return state;
}

void emscripten_main(state_t *state) {
  double dt = time_since_last_tick();
  // Add a new ball every DROP_INTERVAL seconds
  state->time_since_drop += dt;
  if (state->time_since_drop > DROP_INTERVAL) {
    add_ball(state->scene);
    state->time_since_drop = 0.0;
  }
  scene_tick(state->scene, dt);
  sdl_render_scene(state->scene);
}

void emscripten_free(state_t *state) {
  scene_free(state->scene);
  free(state);
}