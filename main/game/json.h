#ifndef GAME_JSON_H
#define GAME_JSON_H

void json_game_from_server( const char* str, game_state_t* game_state );
size_t json_game_to_client( char** str, const game_state_t* game_state );
void json_game_from_client( const char* str, game_state_t* game_state );
void json_game_to_server( char** str, const game_state_t* game_state );

#endif