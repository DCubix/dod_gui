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

	WID body = sys->create(Root{
		.child = sys->create(Layout{
			.top = sys->create(Container{ .height = 50, .background = true }),
			.bottom = sys->create(Container{ .height = 50, .background = true }),
			.left = sys->create(Container{ .width = 50, .background = true }),
			.right = sys->create(Container{ .width = 50, .background = true }),
			.center = sys->create(Container{ .background = true, .child = sys->create(Text{ .text = "The quick brown fox jumped over the lazy dog!" }) })
		})
	});

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
