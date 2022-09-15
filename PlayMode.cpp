#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>

GLuint hexapod_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > hexapod_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("car.pnct"));
	hexapod_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > hexapod_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("car.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		std::cout << mesh_name << std::endl;

		// ignore two cut-out mesh, they are only meaningful in Blender
		if (mesh_name.compare("Cylinder") == 0 || mesh_name.compare("Cylinder.003") == 0) return;

		Mesh const &mesh = hexapod_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = hexapod_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

PlayMode::PlayMode() : scene(*hexapod_scene) {
	// //get pointers to leg for convenience:
	for (auto &transform : scene.transforms) {
		if (transform.name == "Moon") moon = &transform;
		else if (transform.name.find("rabbit") == 0) {
			rabbit_transform.push_back(&transform);
			rabbit_state_array.push_back(inactive);
			rabbit_base_pos.push_back(transform.position);
		}
		else if (transform.name == "Car") {
			car = &transform;
			car_base_pos = transform.position;
		}
	}
	assert(rabbit_transform.size() == rabbit_state_array.size() 
		&& rabbit_state_array.size() == rabbit_base_pos.size());

	if (moon == nullptr) throw std::runtime_error("moon not found.");
	if (car == nullptr) throw std::runtime_error("car not found.");
	if (rabbit_transform.size() < 14) throw std::runtime_error("some rabbits are lost: " + std::to_string(rabbit_transform.size()));

	moon_cur_axis = glm::vec3(0, 0, 1);
	srand(15666u);


	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_ESCAPE) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
			return true;
		} else if (evt.key.keysym.sym == SDLK_a) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
		if (SDL_GetRelativeMouseMode() == SDL_FALSE) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
			return true;
		}
	} else if (evt.type == SDL_MOUSEMOTION) {
		if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			camera->transform->rotation = glm::normalize(
				camera->transform->rotation
				* glm::angleAxis(-motion.x * camera->fovy, glm::vec3(0.0f, 1.0f, 0.0f))
				* glm::angleAxis(motion.y * camera->fovy, glm::vec3(1.0f, 0.0f, 0.0f))
			);
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {

	// rotate the moon
	{
		static float target_z = 0;
		static float time_elapsed = 0;

		time_elapsed += elapsed;
		if (time_elapsed >= 1) {
			time_elapsed = 0;
			target_z = static_cast<float>(rand() % 30) / 100.0f;
		}

		moon_cur_axis = glm::normalize(glm::vec3(1, 0, target_z));
		// moon rotates backward s.t. the car looks like it's moving forward
		static float moon_rot_speed = -float(2 * M_PI) * 0.10f;
		moon_rot_speed -= float(2 * M_PI) * 0.0003f * time_elapsed;

		moon->rotation *= glm::angleAxis(moon_rot_speed * elapsed, moon_cur_axis);
	}

	// rabbit control
	{
		float rabbit_speed = 70;
		float delta_height = rabbit_speed * elapsed;

		float max_height = 30;
		float min_height = 2 * delta_height;

		float max_rising_rabbits = 3;
		float rising_rabbits_count = 0;

		// p is the success rate range from 0 to 1
		auto bernoulli_trial = [](float p) {
			if (p < 0) p = 0.0f;
			if (p > 1) p = 1.0f;
			return static_cast<float>(rand() % 100) / 100.0f <= p;
		};

		// a simple state machine for rabbit movement: inactive->up->down->inactive->...
		for (int i = 0; i < rabbit_transform.size(); i++) {
			// rabbit position update
			if (rabbit_state_array[i] == inactive) {
				if (bernoulli_trial(1.0f - rising_rabbits_count / max_rising_rabbits - 0.2f)) {
					rabbit_state_array[i] = moving_up;
					rising_rabbits_count++;
				}
			}
			else if (rabbit_state_array[i] == moving_up) {
				rising_rabbits_count += 1;
				// rotate up unit vector to object orientation and scale with delta_height
				glm::vec3 delta_height_vec = 
					delta_height * (glm::mat3_cast(rabbit_transform[i]->rotation) * glm::vec3(0,0,1));

				rabbit_transform[i]->position += delta_height_vec;
				if (glm::distance(rabbit_transform[i]->position, rabbit_base_pos[i]) >= max_height) {
					rabbit_state_array[i] = moving_down;
					rising_rabbits_count -= 1;
				}
			}
			else if (rabbit_state_array[i] == moving_down) {
				glm::vec3 delta_height_vec = 
					delta_height * (glm::mat3_cast(rabbit_transform[i]->rotation) * glm::vec3(0,0,1));

				rabbit_transform[i]->position -= delta_height_vec;
				if (glm::distance(rabbit_transform[i]->position, rabbit_base_pos[i]) <= min_height)
					rabbit_state_array[i] = inactive;
			}
			else {
				throw std::runtime_error("unexpected rabbit state");
			}

			// collision detection (just a distance check)
			auto angle_between = [](glm::vec3 a, glm::vec3 b) {
				return glm::acos(glm::dot(a, b));
			};
			bool is_z_aligned = angle_between(glm::mat3_cast(rabbit_transform[i]->rotation) * glm::vec3(0,0,1), 
				car->position - rabbit_transform[i]->position) <= glm::radians(40.0f);
			float collision_dist = is_z_aligned? 8.0f : 5.0f; 
			if (glm::distance(rabbit_transform[i]->position, car->position) < collision_dist) {
				is_game_over = true;
			}
		}
	}

	// car control
	{
		float car_speed = 30.0f;
		float max_displacement = 10.0f;

		float delta_x = 0.0f;
		if (left.pressed && !right.pressed) delta_x += car_speed * elapsed;
		if (!left.pressed && right.pressed) delta_x -= car_speed * elapsed;
		
		glm::vec3 new_pos = car->position + glm::vec3(delta_x, 0, 0);

		if (glm::distance(new_pos, car_base_pos) <= max_displacement) {
			car->position = new_pos;
		}
	}

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	GL_ERRORS(); //print any errors produced by this setup code

	scene.draw(*camera);

	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		constexpr float H = 0.09f;
		lines.draw_text("AD moves the car; Try to dodge all alien rabbits",
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		float ofs = 2.0f / drawable_size.y;
		lines.draw_text("AD moves the car; Try to dodge all alien rabbits",
			glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + + 0.1f * H + ofs, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));

		if (is_game_over) {
			constexpr float H2 = 0.4f;
			lines.draw_text("Game Over",
			glm::vec3(-aspect + 2.5f * H2, -1.0 + 2.5f * H2, 0.0),
			glm::vec3(H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		}
	}
}
