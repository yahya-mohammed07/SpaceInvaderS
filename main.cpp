#include <gl/glew.h>
#include <fmt/core.h>
#include <GLFW/glfw3.h>
#include <array>
#include <memory>
#include <span>
#include <utility>
#include <vector>

// Globals:
bool game_running{ false };
int move_dir{};
int alien_move_dir{ 4 };
bool fire_pressed{};
bool should_change_speed{ false };
int aliens_killed{};
size_t alien_update_timer{};
size_t alien_swarm_position = 24;

//* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
uint32_t xorshift32(uint32_t* rng) {
  uint32_t x = *rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *rng = x;
  return x;
}

double random(uint32_t* rng) {
  return (double)xorshift32(rng) / std::numeric_limits<uint32_t>::max();
}

struct Buffer {
  std::size_t width{}, height{};
  std::vector<uint32_t> m_data{};
};

// 8 bit character
struct Sprite {
  std::size_t width{}, height{};
  uint8_t* m_data{};
};

struct Alien {
  std::size_t x{}, y{};
  uint8_t type{};
};

enum AlienType : uint8_t { DEAD, ALIEN1, ALIEN2, ALIEN3 };

struct Player {
  std::size_t x{}, y{};
  std::size_t lives{};
};

struct Bullet {
  std::size_t x{}, y{};
  int dir{};
};

constexpr int game_max_bullets{ 128 };

struct Game {
  std::size_t width{}, height{};
  std::size_t num_aliens{};
  std::size_t num_bullets{};
  std::vector<Alien> aliens{};
  Player player{};
  std::array<Bullet, game_max_bullets> bullets{};
};

struct Sprite_animation {
  bool loop{};
  std::size_t num_frames{};
  std::size_t frame_duration{};
  std::size_t time{};
  std::vector<Sprite*> frames{};
};

// to get error events, events in glfw reported through callbacks
// this thing is a function pointer...
// GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun cbfun);
/**  err callback func, we use it for `glfwSetErrorCallback`
 * @brief
 *
 * @param error
 * @param description
 */
auto error_callback(int error, const char* description) -> void {
  fmt::print("Error: {} msg: {}\n", error, description);
}

auto key_callback(GLFWwindow* window, int key, int scancode, int action,
  int mods) -> void {
  switch (key) {
  case GLFW_KEY_ESCAPE:
    if (action == GLFW_PRESS) {
      game_running = false;
      break;
    }
  case GLFW_KEY_D: {
    if (action == GLFW_PRESS)
      move_dir += 1;
    else if (action == GLFW_RELEASE)
      move_dir -= 1;
    break;
  }
  case GLFW_KEY_A: {
    if (action == GLFW_PRESS)
      move_dir -= 1;
    else if (action == GLFW_RELEASE)
      move_dir += 1;
    break;
  }
  case GLFW_KEY_SPACE: {
    if (action == GLFW_PRESS)
      fire_pressed = true;
    break;
  }

  default:
    break;
  }
}

/**
 * @brief  sets the left-most 24 bits to the r, g, and b values
 *
 * @param red
 * @param green
 * @param blue
 * @return uint32_t
 */
inline auto rgb_uint32(uint8_t red, uint8_t green, uint8_t blue) -> uint32_t {
  return (red << 24) | (green << 16) | (blue << 8) | 255;
}

/**
 * @brief clears the buffer to a certain color
 * The function iterates over all the pixels and
 * sets each of the pixels to the given color.
 * @param bfr
 * @param color
 */
auto buffer_clear(Buffer& bfr, uint32_t color) -> void {
  for (auto i{ 0u }; i < bfr.width * bfr.height; ++i) {
    bfr.m_data[i] = color;
  }
}

/**
 * @brief
 OpenGL outputs various information during the compilation process, like e.g. a
C++ compiler, but we need to intercept this information. For this I created two
simple functions, validate_shader and validate_program
}
 *
 * @param shader
 * @param file
 */
auto validate_shader(GLuint shader, const char* file = 0) -> void {
  static const unsigned int BUFFER_SIZE = 512;
  char buffer[BUFFER_SIZE];
  GLsizei length = 0;

  glGetShaderInfoLog(shader, BUFFER_SIZE, &length, buffer);

  if (length > 0) {
    fmt::print(" shader {}({}) compile error: {}\n", shader, (file ? file : ""),
      buffer);
  }
}

auto validate_program(GLuint program) -> bool {
  static const GLsizei BUFFER_SIZE = 512;
  GLcharARB buffer[BUFFER_SIZE];
  GLsizei length = 0;

  glGetProgramInfoLog(program, BUFFER_SIZE, &length, buffer);

  if (length > 0) {
    fmt::print("program {} link error: {}\n", program, buffer);
    return false;
  }

  return true;
}

auto buf_sprt_draw(Buffer& bfr, const Sprite& sprt, std::size_t x,
  std::size_t y, uint32_t color) -> void {
  for (size_t xi = 0; xi < sprt.width; ++xi) {
    for (size_t yi = 0; yi < sprt.height; ++yi) {
      auto sy = sprt.height - 1 + y - yi;
      auto sx = x + xi;
      if (sprt.m_data[yi * sprt.width + xi] && sy < bfr.height &&
        sx < bfr.width) {
        bfr.m_data[sy * bfr.width + sx] = color;
      }
    }
  }
}

bool sprite_overlap_check(const Sprite& sp_a, size_t x_a, size_t y_a,
  const Sprite& sp_b, size_t x_b, size_t y_b) {
  if (x_a < x_b + sp_b.width && x_a + sp_a.width > x_b &&
    y_a < y_b + sp_b.height && y_a + sp_a.height > y_b) {
    return true;
  }

  return false;
}

//  takes a piece of text and draws it in the buffer at the specified
//  coordinates and with the specified color
void buffer_draw_text(Buffer& buffer, const Sprite& text_spritesheet,
  const char* text, size_t x, size_t y, uint32_t color) {
  size_t xp = x;
  size_t stride = text_spritesheet.width *
    text_spritesheet.height; // size of one character sprite
  Sprite sprite = text_spritesheet;
  for (const char* charp = text; *charp != '\0'; ++charp) {
    char character = *charp - 32;
    if (character < 0 || character >= 65)
      continue;

    // FIXME:
    sprite.m_data = text_spritesheet.m_data + character * stride;
    buf_sprt_draw(buffer, sprite, xp, y, color);
    xp += sprite.width + 1;
  }
}

// for  drawing numbers
void buffer_draw_number(Buffer& buffer, const Sprite& number_spritesheet,
  size_t number, size_t x, size_t y, uint32_t color) {
  uint8_t digits[64];
  size_t num_digits = 0;

  size_t current_number = number;
  do {
    digits[num_digits++] = current_number % 10;
    current_number = current_number / 10;
  } while (current_number > 0);

  size_t xp = x;
  size_t stride = number_spritesheet.width * number_spritesheet.height;
  Sprite sprite = number_spritesheet;
  for (size_t i = 0; i < num_digits; ++i) {
    uint8_t digit = digits[num_digits - i - 1];

    //// FIXME:
    sprite.m_data = number_spritesheet.m_data + digit * stride;
    buf_sprt_draw(buffer, sprite, xp, y, color);
    xp += sprite.width + 1;
  }
}

auto main() -> int {
  glfwSetErrorCallback(error_callback);
  // init glfw
  if (!glfwInit()) {
    return -1;
  }

  constexpr uint32_t buffer_width{ 224 };
  constexpr uint32_t buffer_height{ 256 };

  // creating window
  auto window = glfwCreateWindow(3 * buffer_width, 3 * buffer_height,
    "Space Invaders", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return -1;
  }

  // using keys
  glfwSetKeyCallback(window, key_callback);

  // making that window appear
  glfwMakeContextCurrent(window);

  // init glew
  if (glewInit() != GLEW_OK) {
    fmt::print("Error initializing `glew`\n");
    return -1;
  }
  //
  const auto clear_color{ rgb_uint32(0, 0, 0) };

  // glClearColor(1.0, 0.0, 0.0, 1.0);
  // Create graphics buffer
  Buffer bfr;
  bfr.height = buffer_height;
  bfr.width = buffer_width;
  bfr.m_data.resize(buffer_width * buffer_height);
  //
  buffer_clear(bfr, clear_color);
  //
  const char* vertex_shader =
    "\n"
    "#version 330\n"
    "\n"
    "noperspective out vec2 TexCoord;\n"
    "\n"
    "void main(void){\n"
    "\n"
    "    TexCoord.x = (gl_VertexID == 2)? 2.0: 0.0;\n"
    "    TexCoord.y = (gl_VertexID == 1)? 2.0: 0.0;\n"
    "    \n"
    "    gl_Position = vec4(2.0 * TexCoord - 1.0, 0.0, 1.0);\n"
    "}\n";

  const char* fragment_shader =
    "\n"
    "#version 330\n"
    "\n"
    "uniform sampler2D buffer;\n"
    "noperspective in vec2 TexCoord;\n"
    "\n"
    "out vec3 outColor;\n"
    "\n"
    "void main(void){\n"
    "    outColor = texture(buffer, TexCoord).rgb;\n"
    "}\n";

  // VAO
  GLuint full_screen_triangle_vao{};
  glGenVertexArrays(1, &full_screen_triangle_vao);
  glBindVertexArray(full_screen_triangle_vao); // bind obj with the name

  // transfer image data to GPU using OpenGL texture
  GLuint buffer_texture;
  glGenTextures(1, &buffer_texture);
  glBindTexture(GL_TEXTURE_2D, buffer_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, bfr.width, bfr.height, 0, GL_RGBA,
    GL_UNSIGNED_INT_8_8_8_8, bfr.m_data.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // creating shaders
  GLuint shader_id = glCreateProgram();
  // vertex shader
  {
    GLuint shader_vp = glCreateShader(GL_VERTEX_SHADER);

    glShaderSource(shader_vp, 1, &vertex_shader, 0);
    glCompileShader(shader_vp);
    validate_shader(shader_vp, vertex_shader);
    glAttachShader(shader_id, shader_vp);

    glDeleteShader(shader_vp);
  }

  // fragment shader
  {
    GLuint shader_fp = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(shader_fp, 1, &fragment_shader, 0);
    glCompileShader(shader_fp);
    validate_shader(shader_fp, fragment_shader);
    glAttachShader(shader_id, shader_fp);

    glDeleteShader(shader_fp);
  }

  glLinkProgram(shader_id);

  if (!validate_program(shader_id)) {
    fmt::print(" error while validating shader.\n");
    glfwTerminate();
    glDeleteVertexArrays(1, &full_screen_triangle_vao);
    return -1;
  }
  glUseProgram(shader_id);

  /**
   * @brief We now need to attach the texture to the uniform sampler2D variable
   * in the fragment shader. OpenGL has a number of texture units to which a
   * uniform can be attached. We get the location of the uniform in the shader
   * (the uniform location can be seen as a kind of "pointer") using
   * glGetUniformLocation, and set the uniform to texture unit '0' using
   * glUniform1i
   *
   */

  GLint location = glGetUniformLocation(
    shader_id, "bfr"); // Returns the location of a uniform variable
  glUniform1i(location, 0);
  glDisable(GL_DEPTH_TEST); // disable server-side GL capabilities
  glActiveTexture(GL_TEXTURE0);

  // creating player

  // creating bullet sprite
  Sprite bullet_sprite;
  bullet_sprite.width = 1;
  bullet_sprite.height = 3;
  bullet_sprite.m_data = new uint8_t[3]{
      1, // @
      1, // @
      1  // @
  };

  Sprite alien_bullet_sprite[2];
  alien_bullet_sprite[0].width = 3;
  alien_bullet_sprite[0].height = 7;
  alien_bullet_sprite[0].m_data = new uint8_t[21]{
      0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0,
  };

  alien_bullet_sprite[1].width = 3;
  alien_bullet_sprite[1].height = 7;
  alien_bullet_sprite[1].m_data = new uint8_t[21]{
      0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0,
  };

  // init game struct
  Game game{};
  game.width = bfr.width;
  game.height = bfr.height;
  game.num_aliens = 55;
  game.aliens.resize(game.num_aliens);
  game.player.x = 112 - 5;
  game.player.y = 32;
  game.player.lives = 3;

  Sprite alien_sprites[6];

  alien_sprites[0].width = 8;
  alien_sprites[0].height = 8;
  alien_sprites[0].m_data = new uint8_t[64]{
      0, 0, 0, 1, 1, 0, 0, 0, // ...@@...
      0, 0, 1, 1, 1, 1, 0, 0, // ..@@@@..
      0, 1, 1, 1, 1, 1, 1, 0, // .@@@@@@.
      1, 1, 0, 1, 1, 0, 1, 1, // @@.@@.@@
      1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@
      0, 1, 0, 1, 1, 0, 1, 0, // .@.@@.@.
      1, 0, 0, 0, 0, 0, 0, 1, // @......@
      0, 1, 0, 0, 0, 0, 1, 0  // .@....@.
  };

  alien_sprites[1].width = 8;
  alien_sprites[1].height = 8;
  alien_sprites[1].m_data = new uint8_t[64]{
      0, 0, 0, 1, 1, 0, 0, 0, // ...@@...
      0, 0, 1, 1, 1, 1, 0, 0, // ..@@@@..
      0, 1, 1, 1, 1, 1, 1, 0, // .@@@@@@.
      1, 1, 0, 1, 1, 0, 1, 1, // @@.@@.@@
      1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@
      0, 0, 1, 0, 0, 1, 0, 0, // ..@..@..
      0, 1, 0, 1, 1, 0, 1, 0, // .@.@@.@.
      1, 0, 1, 0, 0, 1, 0, 1  // @.@..@.@
  };

  alien_sprites[2].width = 11;
  alien_sprites[2].height = 8;
  alien_sprites[2].m_data = new uint8_t[88]{
      0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, // ..@.....@..
      0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, // ...@...@...
      0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, // ..@@@@@@@..
      0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, // .@@.@@@.@@.
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@
      1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, // @.@@@@@@@.@
      1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, // @.@.....@.@
      0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0  // ...@@.@@...
  };

  alien_sprites[3].width = 11;
  alien_sprites[3].height = 8;
  alien_sprites[3].m_data = new uint8_t[88]{
      0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, // ..@.....@..
      1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, // @..@...@..@
      1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, // @.@@@@@@@.@
      1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, // @@@.@@@.@@@
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@
      0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, // .@@@@@@@@@.
      0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, // ..@.....@..
      0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0  // .@.......@.
  };

  alien_sprites[4].width = 12;
  alien_sprites[4].height = 8;
  alien_sprites[4].m_data = new uint8_t[96]{
      0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, // ....@@@@....
      0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, // .@@@@@@@@@@.
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@@
      1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, // @@@..@@..@@@
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@@
      0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, // ...@@..@@...
      0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, // ..@@.@@.@@..
      1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1  // @@........@@
  };

  alien_sprites[5].width = 12;
  alien_sprites[5].height = 8;
  alien_sprites[5].m_data = new uint8_t[96]{
      0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, // ....@@@@....
      0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, // .@@@@@@@@@@.
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@@
      1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, // @@@..@@..@@@
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@@
      0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, // ..@@@..@@@..
      0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, // .@@..@@..@@.
      0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0  // ..@@....@@..
  };

  Sprite alien_death_sprite;
  alien_death_sprite.width = 13;
  alien_death_sprite.height = 7;
  alien_death_sprite.m_data = new uint8_t[91]{
      0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, // .@..@...@..@.
      0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, // ..@..@.@..@..
      0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, // ...@.....@...
      1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, // @@.........@@
      0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, // ...@.....@...
      0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, // ..@..@.@..@..
      0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0  // .@..@...@..@.
  };

  Sprite player_sprite;
  player_sprite.width = 11;
  player_sprite.height = 7;
  player_sprite.m_data = new uint8_t[77]{
      0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, // .....@.....
      0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, // ....@@@....
      0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, // ....@@@....
      0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, // .@@@@@@@@@.
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // @@@@@@@@@@@
  };

  // positioning aliens
  for (size_t i = 0; i < 5; ++i) {
    for (size_t j = 0; j < 11; ++j) {
      game.aliens[i * 11 + j].x = 17 * j + 22;
      game.aliens[i * 11 + j].y = 17 * i + 128;
    }
  }

  // spread the aliens
  for (size_t yi = 0; yi < 5; ++yi) {
    for (size_t xi = 0; xi < 11; ++xi) {
      Alien& alien = game.aliens[yi * 11 + xi];
      alien.type = (5 - yi) / 2 + 1;

      const Sprite& sprite = alien_sprites[2 * (alien.type - 1)];

      alien.x = 16 * xi + alien_swarm_position +
        (alien_death_sprite.width - sprite.width) / 2;
      alien.y = 17 * yi + 128;
    }
  }

  // alien death counter... 55 should constant
  std::array<uint8_t, 55> alien_death_counter{};
  for (size_t i = 0; i < 55; ++i) {
    alien_death_counter[i] = 10;
  }
  // creating animation for the aliens
  std::array<Sprite_animation, 3> alien_animation{};

  for (size_t i{}; i < alien_animation.size(); ++i) {
    alien_animation[i].loop = true;
    alien_animation[i].num_frames = 2;
    alien_animation[i].frame_duration = 10;
    alien_animation[i].time = 0;

    // defining frames
    alien_animation[i].frames.resize(2);
    alien_animation[i].frames[0] = &alien_sprites[2 * i];
    alien_animation[i].frames[1] = &alien_sprites[2 * i + 1];
  }

  Sprite text_spritesheet;
  text_spritesheet.width = 5;
  text_spritesheet.height = 7;
  text_spritesheet.m_data = new uint8_t[65 * 35]{
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0,
      0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0,
      0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0,
      1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0,
      1, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0,
      1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0,
      0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0,
      0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0,
      0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0,
      0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0,
      0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0,
      0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0,
      1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0,
      1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
      0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
      0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0,
      0, 1, 0, 0, 0, 0, 1, 0, 0, 0,

      0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0, 1,
      1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0,
      0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0,
      1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0,
      0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0,
      0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
      1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
      1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0,
      1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1,
      0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0,
      0, 1, 0, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0,
      1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1,
      1, 0, 0, 0, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0,

      0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1,
      0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
      0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
      1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0,
      0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0,
      0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 1, 0, 1,
      1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0,

      0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1,
      1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1,
      1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0,
      1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1,
      0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1,
      1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
      1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0,
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0,
      1, 0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1,
      1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1,
      1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0,
      0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
      0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0,
      1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 0, 0,
      1, 0, 0, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0,
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1,
      1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1,
      1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 1, 0, 1,
      1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1,
      1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0,
      1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0,
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1,
      1, 0, 0, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0,
      1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0,
      1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0,
      1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0,
      0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0,
      1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1,
      1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1,
      1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1,
      1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1,
      1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0,
      0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1,
      0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0,
      1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
      1, 0, 0, 0, 0, 1, 1, 1, 1, 1,

      0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0,
      0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0,
      0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0,
      0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0,
      1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
      0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

  Sprite number_spritesheet = text_spritesheet;
  number_spritesheet.m_data += 16 * 35;

  Sprite_animation alien_bullet_animation;
  alien_bullet_animation.loop = true;
  alien_bullet_animation.num_frames = 2;
  alien_bullet_animation.frame_duration = 5;
  alien_bullet_animation.time = 0;

  alien_bullet_animation.frames.resize(2);
  alien_bullet_animation.frames[0] = &alien_bullet_sprite[0];
  alien_bullet_animation.frames[1] = &alien_bullet_sprite[1];

  // V-Sync
  glfwSwapInterval(1);

  // control player direction movement
  uint32_t rng = 13;
  int player_move{};
  int alien_move{};
  size_t score = 0;
  unsigned int credits = 0;
  size_t alien_swarm_max_position = game.width - 16 * 11 - 3;

  // indicates the game is still running
  game_running = true;
  size_t alien_update_frequency = 120;
  // creating the game loop

  auto main_loop = [&]() {
    while (!glfwWindowShouldClose(window) && game_running) {

      buffer_clear(bfr, clear_color);

      if (game.player.lives == 0) {

        buffer_draw_text(bfr, text_spritesheet, "GAME OVER",
          game.width / 2 - 30, game.height / 2,
          rgb_uint32(128, 0, 0));
        buffer_draw_text(bfr, text_spritesheet, "PRESS R TO START OVER",
          game.width / 2 - 70, game.height / 2 - 50,
          rgb_uint32(128, 0, 0));
        buffer_draw_text(bfr, text_spritesheet, "SCORE", 4,
          game.height - text_spritesheet.height - 7,
          rgb_uint32(128, 0, 0));
        buffer_draw_number(bfr, number_spritesheet, score,
          4 + 2 * number_spritesheet.width,
          game.height - 2 * number_spritesheet.height - 12,
          rgb_uint32(128, 0, 0));

        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, bfr.width, bfr.height, GL_RGBA,
          GL_UNSIGNED_INT_8_8_8_8, bfr.m_data.data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glfwSwapBuffers(window);
        glfwPollEvents();
        continue;
      }

      // Draw
      buffer_draw_text(bfr, text_spritesheet, "SCORE", 4,
        game.height - text_spritesheet.height - 7,
        rgb_uint32(128, 0, 0));

      buffer_draw_number(bfr, number_spritesheet, score,
        4 + 2 * number_spritesheet.width,
        game.height - 2 * number_spritesheet.height - 12,
        rgb_uint32(128, 0, 0));

      {
        char credit_text[16];
        sprintf_s(credit_text, "CREDIT %02u", credits);
        buffer_draw_text(bfr, text_spritesheet, credit_text, 164, 7,
          rgb_uint32(128, 0, 0));
      }

      // draw lives
      buffer_draw_number(bfr, number_spritesheet, game.player.lives, 4, 7,
        rgb_uint32(128, 0, 0));
      size_t xp = 11 + number_spritesheet.width;
      for (size_t i = 0; i < game.player.lives; ++i) {
        buf_sprt_draw(bfr, player_sprite, xp, 7, rgb_uint32(128, 0, 0));
        xp += player_sprite.width + 2;
      }
      //
      for (size_t i = 0; i < game.width; ++i) {
        bfr.m_data[game.width * 16 + i] = rgb_uint32(128, 0, 0);
      }

      // drawing alien and bullet sprites each animation
      for (size_t i = 0; i < game.num_aliens; ++i) {
        if (!alien_death_counter[i]) // check death counts
          continue;

        const auto& alien = game.aliens[i];
        if (alien.type == AlienType::DEAD) {
          buf_sprt_draw(bfr, alien_death_sprite, alien.x, alien.y,
            rgb_uint32(128, 0, 0));
        }
        else {
          const auto& animation = alien_animation[alien.type - 1];
          auto current_frame = animation.time / animation.frame_duration;
          const auto& sprite = *animation.frames[current_frame];
          buf_sprt_draw(bfr, sprite, alien.x, alien.y, rgb_uint32(0, 128, 0));
        }
      }

      for (size_t i = 0; i < game.num_bullets; ++i) {
        const auto& bullet = game.bullets[i];
        const Sprite* sprite;
        if (bullet.dir > 0)
          sprite = &bullet_sprite;
        else {
          size_t c = alien_bullet_animation.time /
            alien_bullet_animation.frame_duration;
          sprite = &alien_bullet_sprite[c];
        }
        buf_sprt_draw(bfr, *sprite, bullet.x, bullet.y, rgb_uint32(128, 0, 0));
      }

      // drawing player one time
      buf_sprt_draw(bfr, player_sprite, game.player.x, game.player.y,
        rgb_uint32(128, 0, 0));

      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, bfr.width, bfr.height, GL_RGBA,
        GL_UNSIGNED_INT_8_8_8_8, bfr.m_data.data());
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glfwSwapBuffers(
        window); // double buffering scheme, front to display image
      // back to drawing, the buffers swapped each itr using this func

      // update aliens position after going beyond y
      if (alien_update_frequency-- == 0) {
        alien_update_frequency = 120;
        for (size_t i = 0; i < game.num_aliens; ++i) {
          auto& alien = game.aliens[i];
          if (alien.type == AlienType::DEAD)
            continue;
          if (alien.y > game.height) {
            alien.y = 0;
            alien.x = rng % alien_swarm_max_position;
            alien.type = AlienType::DEAD;
            alien_move_dir -= 1;
          }
          else {
            alien.y += alien_move_dir;
          }
        }
      }

      // Simulate bullets
      for (size_t i = 0; i < game.num_bullets; ++i) {
        game.bullets[i].y += game.bullets[i].dir;
        if (game.bullets[i].y >= game.height ||
          game.bullets[i].y < bullet_sprite.height) {
          game.bullets[i] = game.bullets[game.num_bullets - 1];
          --game.num_bullets;
          continue;
        }

        // Alien bullet
        if (game.bullets[i].dir < 0) {
          bool overlap = sprite_overlap_check(
            alien_bullet_sprite[0], game.bullets[i].x, game.bullets[i].y,
            player_sprite, game.player.x, game.player.y);

          if (overlap) {
            --game.player.lives;
            game.bullets[i] = game.bullets[game.num_bullets - 1];
            --game.num_bullets;
            break;
          }
        }
        // Player bullet
        else {
          // Check if player bullet hits an alien bullet
          for (size_t i = 0; i < game.num_bullets; ++i) {
            if (i == i)
              continue;

            bool overlap = sprite_overlap_check(
              bullet_sprite, game.bullets[i].x, game.bullets[i].y,
              alien_bullet_sprite[0], game.bullets[i].x, game.bullets[i].y);

            if (overlap) {
              if (i == game.num_bullets - 1) {
                game.bullets[i] = game.bullets[game.num_bullets - 2];
              }
              else if (i == game.num_bullets - 1) {
                game.bullets[i] = game.bullets[game.num_bullets - 2];
              }
              else {
                game.bullets[(i < i) ? i : i] =
                  game.bullets[game.num_bullets - 1];
                game.bullets[(i < i) ? i : i] =
                  game.bullets[game.num_bullets - 2];
              }
              game.num_bullets -= 2;
              break;
            }
          }

          // Check hit
          for (size_t j = 0; j < game.num_aliens; ++j) {
            const auto& alien = game.aliens[j];
            if (alien.type == AlienType::DEAD)
              continue;

            const auto& animation = alien_animation[alien.type - 1];
            auto current_frame = animation.time / animation.frame_duration;
            const auto& alien_sprite = *animation.frames[current_frame];

            if (sprite_overlap_check(bullet_sprite, game.bullets[i].x,
              game.bullets[i].y, alien_sprite, alien.x,
              alien.y)) {
              score += 10 * (4 - game.aliens[j].type);
              game.aliens[j].type = AlienType::DEAD;
              game.aliens[j].x -=
                (alien_death_sprite.width - alien_sprite.width) / 2;
              game.bullets[i] = game.bullets[game.num_bullets - 1];
              --game.num_bullets;
              ++aliens_killed;

              if (aliens_killed % 15 == 0)
                should_change_speed = true;

              break;
            }
          }
        }
      }

      // Simulate aliens
      if (should_change_speed) {
        should_change_speed = false;
        alien_update_frequency /= 2;
        for (size_t i = 0; i < 3; ++i) {
          alien_animation[i].frame_duration = alien_update_frequency;
        }
      }

      // counting deaths
      for (size_t i = 0; i < game.num_aliens; ++i) {

        if (game.aliens[i].type == AlienType::DEAD && alien_death_counter[i]) {
          --alien_death_counter[i];
        }
      }

      if (alien_update_timer >= alien_update_frequency) {
        alien_update_timer = 0;

        if ((int)alien_swarm_position + alien_move_dir < 0) {
          alien_move_dir *= -1;

          for (size_t ai = 0; ai < game.num_aliens; ++ai) {
            auto& alien = game.aliens[ai];
            alien.y -= 8; // goes down 8 pixels
          }
        }
        else if (alien_swarm_position >
          alien_swarm_max_position - alien_move_dir) {
          alien_move_dir *= -1;
        }
        alien_swarm_position += alien_move_dir;

        for (size_t ai = 0; ai < game.num_aliens; ++ai) {
          auto& alien = game.aliens[ai];
          alien.x += alien_move_dir;
        }

        if (aliens_killed < game.num_aliens) {
          size_t rai = static_cast<size_t>(game.num_aliens * random(&rng));
          while (game.aliens[rai].type == AlienType::DEAD) {
            rai = static_cast<size_t>(game.num_aliens * random(&rng));
          }
          const auto& alien_sprite =
            *alien_animation[game.aliens[rai].type - 1].frames[0];
          game.bullets[game.num_bullets].x =
            game.aliens[rai].x + alien_sprite.width / 2;
          game.bullets[game.num_bullets].y =
            game.aliens[rai].y - alien_bullet_sprite[0].height;
          game.bullets[game.num_bullets].dir = -2;
          ++game.num_bullets;
        }
      }

      // Update animations
      for (size_t i = 0; i < 3; ++i) {
        ++alien_animation[i].time;
        if (alien_animation[i].time >=
          alien_animation[i].num_frames * alien_animation[i].frame_duration) {
          alien_animation[i].time = 0;
        }
      }
      ++alien_bullet_animation.time;
      if (alien_bullet_animation.time >=
        alien_bullet_animation.num_frames *
        alien_bullet_animation.frame_duration) {
        alien_bullet_animation.time = 0;
      }

      ++alien_update_timer;

      // simulating player
      player_move = 2 * move_dir;
      if (player_move != 0) {
        if (game.player.x + player_sprite.width + player_move >= game.width) {
          game.player.x = game.width - player_sprite.width;
        }
        else if (static_cast<int>(game.player.x) + player_move <= 0) {
          game.player.x = 0;
        }
        else {
          game.player.x += player_move;
        }
      }
      //
      if (aliens_killed < game.num_aliens) {
        size_t ai = 0;
        while (game.aliens[ai].type == AlienType::DEAD)
          ++ai;
        const auto& sprite = alien_sprites[2 * (game.aliens[ai].type - 1)];
        size_t pos =
          game.aliens[ai].x - (alien_death_sprite.width - sprite.width) / 2;
        if (pos > alien_swarm_position)
          alien_swarm_position = pos;

        ai = game.num_aliens - 1;
        while (game.aliens[ai].type == AlienType::DEAD)
          --ai;
        pos = game.width - game.aliens[ai].x - 13 + pos;
        if (pos > alien_swarm_max_position)
          alien_swarm_max_position = pos;
      }
      else {
        alien_update_frequency = 120;
        alien_swarm_position = 24;

        aliens_killed = 0;
        alien_update_timer = 0;

        alien_move_dir = 4;

        for (size_t xi = 0; xi < 11; ++xi) {
          for (size_t yi = 0; yi < 5; ++yi) {
            size_t ai = xi * 5 + yi;

            alien_death_counter[ai] = 10;

            auto& alien = game.aliens[ai];
            alien.type = (5 - yi) / 2 + 1;

            const auto& sprite = alien_sprites[2 * (alien.type - 1)];

            alien.x = 16 * xi + alien_swarm_position +
              (alien_death_sprite.width - sprite.width) / 2;
            alien.y = 17 * yi + 128;
          }
        }
      }

      // process events
      if (fire_pressed && game.num_bullets < game_max_bullets) {
        game.bullets[game.num_bullets].x =
          game.player.x + player_sprite.width / 2;
        game.bullets[game.num_bullets].y = game.player.y;
        game.bullets[game.num_bullets].dir = 2;
        ++game.num_bullets;
      }
      fire_pressed = false;

      glfwPollEvents(); // terminates the loop if user intended to
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    glDeleteVertexArrays(1, &full_screen_triangle_vao);
    for (size_t i = 0; i < 6; ++i) {
      delete[] alien_sprites[i].m_data;
    }

    delete[] text_spritesheet.m_data;
    delete[] alien_death_sprite.m_data;

    return true;
    }; // end game loop

  main_loop(); // start the game loop
}