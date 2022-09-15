#include "Mode.hpp"

#include "Scene.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <stdlib.h>  // for random number generator
#include <random>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	// current axis of rotation
	Scene::Transform *moon = nullptr;
	glm::vec3 moon_cur_axis;

	Scene::Transform *car = nullptr;
	glm::vec3 car_base_pos;

	enum rabbit_state {
		inactive = 0,
		moving_up = 1,
		moving_down = 2
	};

	std::vector<rabbit_state> rabbit_state_array;
	std::vector<Scene::Transform *> rabbit_transform;
	std::vector<glm::vec3> rabbit_base_pos;

	//hexapod leg to wobble:
	// Scene::Transform *hip = nullptr;
	// Scene::Transform *upper_leg = nullptr;
	// Scene::Transform *lower_leg = nullptr;
	// glm::quat hip_base_rotation;
	// glm::quat upper_leg_base_rotation;
	// glm::quat lower_leg_base_rotation;
	// float wobble = 0.0f;
	
	//camera:
	Scene::Camera *camera = nullptr;

	bool is_game_over = false;
};
