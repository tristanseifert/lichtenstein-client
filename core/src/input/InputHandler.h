#ifndef INPUTHANDLER_H
#define INPUTHANDLER_H

class INIReader;
class InputPlugin;
class LichtensteinPluginHandler;

class InputHandler {
	public:
		InputHandler(INIReader *config, LichtensteinPluginHandler *pluginHandler);
		~InputHandler();

	private:
		void loadPlugin(void);

	private:
		INIReader *config = nullptr;

		LichtensteinPluginHandler *pluginHandler = nullptr;
		InputPlugin *plugin = nullptr;
};

#endif
