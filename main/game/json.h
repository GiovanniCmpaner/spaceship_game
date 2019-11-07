#ifndef GAME_JSON_H
#define GAME_JSON_H

void json_game_from_server( game_state_t* game_state, const char* buffer, size_t length );
size_t json_game_to_client( const game_state_t* game_state, char* buffer, size_t length );
void json_game_from_client( game_state_t* game_state, const char* buffer, size_t length );
void json_game_to_server  ( const game_state_t* game_state, char* buffer, size_t length );

#endif