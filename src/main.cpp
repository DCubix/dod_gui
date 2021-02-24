#include <iostream>
#include <memory>

#include "sdl.h"
#include "ui.h"

int main(int argc, const char** argv) {
	SDL_Init(SDL_INIT_EVERYTHING);

	SDL_Window* win = SDL_CreateWindow("Test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_SHOWN);
	SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

	std::unique_ptr<Device> dev = std::make_unique<Device>(win, ren);
	dev->loadSkin("../gui.bmp");

	std::unique_ptr<UISystem> sys = std::make_unique<UISystem>();

	bool running = true;

	WID body = sys->loadUI("../test.ui");
	sys->get<Button>("btn")->onPressed = [&]() {
		std::string msg = std::string("Hello, ") + sys->get<Input>("name")->text;
		SDL_ShowSimpleMessageBox(0, "Pressed", msg.c_str(), win);
	};

	SDL_Event e;
	while (running) {
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) running = false;
			sys->processEvents(*dev, e, body);
		}

		sys->draw(*dev, body, Context());

		dev->flush();

		SDL_RenderPresent(ren);
	}

	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}
