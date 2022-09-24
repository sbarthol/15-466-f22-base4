#include "Mode.hpp"

#include "Scene.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>

#include <hb.h>
#include <hb-ft.h>

#include <vector>
#include <deque>

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

	//hexapod leg to wobble:
	Scene::Transform *hip = nullptr;
	Scene::Transform *upper_leg = nullptr;
	Scene::Transform *lower_leg = nullptr;
	glm::quat hip_base_rotation;
	glm::quat upper_leg_base_rotation;
	glm::quat lower_leg_base_rotation;
	float wobble = 0.0f;

	glm::vec3 get_leg_tip_position();

	//music coming from the tip of the leg (as a demonstration):
	std::shared_ptr< Sound::PlayingSample > leg_tip_loop;
	
	//camera:
	Scene::Camera *camera = nullptr;

	void draw_text(std::string s);

	GLuint texture{0}, sampler{0};
  GLuint vbo{0}, vao{0};
  GLuint vs{0}, fs{0}, program{0};
	GLuint texUniform{0};

  FT_Face ft_face;
	FT_Library ft_library;

	hb_font_t *hb_font;

	const char *VERTEX_SHADER = ""
        "#version 330\n"
        "in vec4 position;\n"
        "out vec2 texCoords;\n"
        "void main(void) {\n"
        "    gl_Position = vec4(position.xy, 0, 1);\n"
        "    texCoords = position.zw;\n"
        "}\n";


	const char *FRAGMENT_SHADER = ""
        "#version 330\n"
        "uniform sampler2D tex;\n"
        "in vec2 texCoords;\n"
        "out vec4 fragColor;\n"
				"const vec4 color = vec4(1, 1, 1, 1);\n"
        "void main(void) {\n"
        "    fragColor = vec4(1, 1, 1, texture(tex, texCoords).r) * color;\n"
        "}\n";

};
