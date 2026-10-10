#pragma once
enum class BarkState { Starting, Ready, Playing, Error };
void bark_begin();
bool bark_request();
BarkState bark_state();
