/*
===========================================================================

Wolfenstein: Enemy Territory GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Wolfenstein: Enemy Territory GPL Source Code (Wolf ET Source Code).  

Wolf ET Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Wolf ET Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Wolf ET Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Wolf: ET Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Wolf ET Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/


#include "server.h"

/*
===============================================================================

OPERATOR CONSOLE ONLY COMMANDS

These commands can only be entered from stdin or by a remote operator datagram
===============================================================================
*/


/*
==================
SV_GetPlayerByHandle

Returns the player with player id or name from Cmd_Argv(1)
==================
*/
static client_t *SV_GetPlayerByHandle( void ) {
	client_t	*cl;
	int			i;
	char		*s;
	char		cleanName[64];

	// make sure server is running
	if ( !com_sv_running->integer ) {
		return NULL;
	}

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "No player specified.\n" );
		return NULL;
	}

	s = Cmd_Argv(1);

	// Check whether this is a numeric player handle
	for(i = 0; s[i] >= '0' && s[i] <= '9'; i++);
	
	if(!s[i])
	{
		int plid = atoi(s);

		// Check for numeric playerid match
		if(plid >= 0 && plid < sv_maxclients->integer)
		{
			cl = &svs.clients[plid];
			
			if(cl->state)
				return cl;
		}
	}

	// check for a name match
	for ( i=0, cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++ ) {
		if ( !cl->state ) { // <= CS_ZOMBIE
			continue;
		}
		if ( !Q_stricmp( cl->name, s ) ) {
			return cl;
		}

		Q_strncpyz( cleanName, cl->name, sizeof(cleanName) );
		Q_CleanStr( cleanName );
		if ( !Q_stricmp( cleanName, s ) ) {
			return cl;
		}
	}

	Com_Printf( "Player %s is not on the server\n", s );

	return NULL;
}

/*
==================
SV_GetPlayerByNum

Returns the player with idnum from Cmd_Argv(1)
==================
*/
static client_t *SV_GetPlayerByNum( void ) {
	client_t	*cl;
	int			i;
	int			idnum;
	char		*s;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		return NULL;
	}

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "No player specified.\n" );
		return NULL;
	}

	s = Cmd_Argv(1);

	for (i = 0; s[i]; i++) {
		if (s[i] < '0' || s[i] > '9') {
			Com_Printf( "Bad slot number: %s\n", s);
			return NULL;
		}
	}
	idnum = atoi( s );
	if ( idnum < 0 || idnum >= sv_maxclients->integer ) {
		Com_Printf( "Bad client slot: %i\n", idnum );
		return NULL;
	}

	cl = &svs.clients[idnum];
	if ( !cl->state ) { // <= CS_ZOMBIE
		Com_Printf( "Client %i is not active\n", idnum );
		return NULL;
	}
	return cl;
}

//=========================================================


/*
==================
SV_Map_f

Restart the server on a different map
==================
*/
static void SV_Map_f( void ) {
	char        *cmd;
	char        *map;
	char smapname[MAX_QPATH];
	char mapname[MAX_QPATH];
	qboolean killBots, cheat, buildScript;
	char expanded[MAX_QPATH];
	int savegameTime = -1;
	const char *cl_profileStr = Cvar_VariableString( "cl_profile" );

	map = Cmd_Argv(1);
	if ( !map || !*map ) {
		return;
	}

	if ( !com_gameInfo.spEnabled ) {
		if ( !Q_stricmp( Cmd_Argv( 0 ), "spdevmap" ) || !Q_stricmp( Cmd_Argv( 0 ), "spmap" ) ) {
			Com_Printf( "Single Player is not enabled.\n" );
			return;
		}
	}

	buildScript = Cvar_VariableIntegerValue( "com_buildScript" );

	if ( SV_GameIsSinglePlayer() ) {
		if ( !buildScript && sv_reloading->integer && sv_reloading->integer != RELOAD_NEXTMAP ) { // game is in 'reload' mode, don't allow starting new maps yet.
			return;
		}

		// Trap a savegame load
		if ( strstr( map, ".sav" ) ) {
			// open the savegame, read the mapname, and copy it to the map string
			char savemap[MAX_QPATH];
			char savedir[MAX_QPATH];
			byte *buffer;
			int size, csize;

			if ( com_gameInfo.usesProfiles && cl_profileStr[0] ) {
				Com_sprintf( savedir, sizeof( savedir ), "profiles/%s/save/", cl_profileStr );
			} else {
				Q_strncpyz( savedir, "save/", sizeof( savedir ) );
			}

			if ( !( strstr( map, savedir ) == map ) ) {
				Com_sprintf( savemap, sizeof( savemap ), "%s%s", savedir, map );
			} else {
				strcpy( savemap, map );
			}

			size = FS_ReadFile( savemap, NULL );
			if ( size < 0 ) {
				Com_Printf( "Can't find savegame %s\n", savemap );
				return;
			}

			//buffer = Hunk_AllocateTempMemory(size);
			FS_ReadFile( savemap, (void **)&buffer );

			if ( Q_stricmp( savemap, va( "%scurrent.sav", savedir ) ) != 0 ) {
				// copy it to the current savegame file
				FS_WriteFile( va( "%scurrent.sav", savedir ), buffer, size );
				// make sure it is the correct size
				csize = FS_ReadFile( va( "%scurrent.sav", savedir ), NULL );
				if ( csize != size ) {
					Hunk_FreeTempMemory( buffer );
					FS_Delete( va( "%scurrent.sav", savedir ) );
// TTimo
#ifdef __linux__
					Com_Error( ERR_DROP, "Unable to save game.\n\nPlease check that you have at least 5mb free of disk space in your home directory." );
#else
					Com_Error( ERR_DROP, "Insufficient free disk space.\n\nPlease free at least 5mb of free space on game drive." );
#endif
					return;
				}
			}

			// set the cvar, so the game knows it needs to load the savegame once the clients have connected
			Cvar_Set( "savegame_loading", "1" );
			// set the filename
			Cvar_Set( "savegame_filename", savemap );

			// the mapname is at the very start of the savegame file
			Com_sprintf( savemap, sizeof( savemap ), ( char * )( buffer + sizeof( int ) ) );  // skip the version
			Q_strncpyz( smapname, savemap, sizeof( smapname ) );
			map = smapname;

			savegameTime = *( int * )( buffer + sizeof( int ) + MAX_QPATH );

			if ( savegameTime >= 0 ) {
				svs.time = savegameTime;
			}

			Hunk_FreeTempMemory( buffer );
		} else {
			Cvar_Set( "savegame_loading", "0" );  // make sure it's turned off
			// set the filename
			Cvar_Set( "savegame_filename", "" );
		}
	} else {
		Cvar_Set( "savegame_loading", "0" );  // make sure it's turned off
		// set the filename
		Cvar_Set( "savegame_filename", "" );
	}

	// make sure the level exists before trying to change, so that
	// a typo at the server console won't end the game
	Com_sprintf (expanded, sizeof(expanded), "maps/%s.bsp", map);
	if ( FS_ReadFile (expanded, NULL) == -1 ) {
		Com_Printf ("Can't find map %s\n", expanded);
		return;
	}

	Cvar_Set( "gamestate", va( "%i", GS_INITIALIZE ) );       // NERVE - SMF - reset gamestate on map/devmap

	Cvar_Set( "g_currentRound", "0" );            // NERVE - SMF - reset the current round
	Cvar_Set( "g_nextTimeLimit", "0" );           // NERVE - SMF - reset the next time limit

	// START	Mad Doctor I changes, 8/14/2002.  Need a way to force load a single player map as single player
	if ( !Q_stricmp( Cmd_Argv( 0 ), "spdevmap" ) || !Q_stricmp( Cmd_Argv( 0 ), "spmap" ) ) {
		// This is explicitly asking for a single player load of this map
		Cvar_Set( "g_gametype", va( "%i", com_gameInfo.defaultSPGameType ) );
		// force latched values to get set
		Cvar_Get( "g_gametype", va( "%i", com_gameInfo.defaultSPGameType ), CVAR_SERVERINFO | CVAR_USERINFO | CVAR_LATCH );
		// enable bot support for AI
		Cvar_Set( "bot_enable", "1" );
	}

	// Rafael gameskill
//	Cvar_Get ("g_gameskill", "3", CVAR_SERVERINFO | CVAR_LATCH);
	// done

	cmd = Cmd_Argv( 0 );

	if ( !Q_stricmp( cmd, "devmap" ) ) {
		cheat = qtrue;
		killBots = qtrue;
	} else
	if ( !Q_stricmp( Cmd_Argv( 0 ), "spdevmap" ) ) {
		cheat = qtrue;
		killBots = qtrue;
	} else
	{
		cheat = qfalse;
		killBots = qfalse;
	}

	// save the map name here cause on a map restart we reload the q3config.cfg
	// and thus nuke the arguments of the map command
	Q_strncpyz(mapname, map, sizeof(mapname));

	// start up the map
	SV_SpawnServer( mapname, killBots );

	// set the cheat value
	// if the level was started with "map <levelname>", then
	// cheats will not be allowed.  If started with "devmap <levelname>"
	// then cheats will be allowed
	if ( cheat ) {
		Cvar_Set( "sv_cheats", "1" );
	} else {
		Cvar_Set( "sv_cheats", "0" );
	}
}

/*
================
SV_CheckTransitionGameState

NERVE - SMF
================
*/
static qboolean SV_CheckTransitionGameState( gamestate_t new_gs, gamestate_t old_gs ) {
	if ( old_gs == new_gs && new_gs != GS_PLAYING ) {
		return qfalse;
	}

//	if ( old_gs == GS_WARMUP && new_gs != GS_WARMUP_COUNTDOWN )
//		return qfalse;

//	if ( old_gs == GS_WARMUP_COUNTDOWN && new_gs != GS_PLAYING )
//		return qfalse;

	if ( old_gs == GS_WAITING_FOR_PLAYERS && new_gs != GS_WARMUP ) {
		return qfalse;
	}

	if ( old_gs == GS_INTERMISSION && new_gs != GS_WARMUP ) {
		return qfalse;
	}

	if ( old_gs == GS_RESET && ( new_gs != GS_WAITING_FOR_PLAYERS && new_gs != GS_WARMUP ) ) {
		return qfalse;
	}

	return qtrue;
}

/*
================
SV_TransitionGameState

NERVE - SMF
================
*/
static qboolean SV_TransitionGameState( gamestate_t new_gs, gamestate_t old_gs, int delay ) {
	if ( !SV_GameIsSinglePlayer() && !SV_GameIsCoop() ) {
		// we always do a warmup before starting match
		if ( old_gs == GS_INTERMISSION && new_gs == GS_PLAYING ) {
			new_gs = GS_WARMUP;
		}
	}

	// check if its a valid state transition
	if ( !SV_CheckTransitionGameState( new_gs, old_gs ) ) {
		return qfalse;
	}

	if ( new_gs == GS_RESET ) {
		new_gs = GS_WARMUP;
	}

	Cvar_Set( "gamestate", va( "%i", new_gs ) );

	return qtrue;
}

#ifdef MSG_FIELDINFO_SORT_DEBUG
void MSG_PrioritiseEntitystateFields( void );
void MSG_PrioritisePlayerStateFields( void );

static void SV_FieldInfo_f( void ) {
	MSG_PrioritiseEntitystateFields();
	MSG_PrioritisePlayerStateFields();
}
#endif

/*
================
SV_MapRestart_f

Completely restarts a level, but doesn't send a new gamestate to the clients.
This allows fair starts with variable load times.
================
*/
static void SV_MapRestart_f( void ) {
	int i;
	client_t    *client;
	char        *denied;
	qboolean isBot;
	int delay = 0;
	gamestate_t new_gs, old_gs;     // NERVE - SMF

	// make sure we aren't restarting twice in the same frame
	if ( com_frameTime == sv.serverId ) {
		return;
	}

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	// ydnar: allow multiple delayed server restarts [atvi bug 3813]
	//%	if ( sv.restartTime ) {
	//%		return;
	//%	}

	if ( Cmd_Argc() > 1 ) {
		delay = atoi( Cmd_Argv( 1 ) );
	}

	if ( delay ) {
		sv.restartTime = sv.time + delay * 1000;
		SV_SetConfigstring( CS_WARMUP, va( "%i", sv.restartTime ) );
		return;
	}

	// NERVE - SMF - read in gamestate or just default to GS_PLAYING
	old_gs = atoi( Cvar_VariableString( "gamestate" ) );

	if ( SV_GameIsSinglePlayer() || SV_GameIsCoop() ) {
		new_gs = GS_PLAYING;
	} else {
		if ( Cmd_Argc() > 2 ) {
			new_gs = atoi( Cmd_Argv( 2 ) );
		} else {
			new_gs = GS_PLAYING;
		}
	}

	if ( !SV_TransitionGameState( new_gs, old_gs, delay ) ) {
		return;
	}

	// check for changes in variables that can't just be restarted
	// check for maxclients change
	if ( sv_maxclients->modified ) {
		char mapname[MAX_QPATH];

		Com_Printf( "sv_maxclients variable change -- restarting.\n" );
		// restart the map the slow way
		Q_strncpyz( mapname, Cvar_VariableString( "mapname" ), sizeof( mapname ) );

		SV_SpawnServer( mapname, qfalse );
		return;
	}

	// Check for loading a saved game
	if ( Cvar_VariableIntegerValue( "savegame_loading" ) ) {
		// open the current savegame, and find out what the time is, everything else we can ignore
		char savemap[MAX_QPATH];
		byte *buffer;
		int size, savegameTime;
		const char *cl_profileStr = Cvar_VariableString( "cl_profile" );

		if ( com_gameInfo.usesProfiles ) {
			Com_sprintf( savemap, sizeof( savemap ), "profiles/%s/save/current.sav", cl_profileStr );
		} else {
			Q_strncpyz( savemap, "save/current.sav", sizeof( savemap ) );
		}

		size = FS_ReadFile( savemap, NULL );
		if ( size < 0 ) {
			Com_Printf( "Can't find savegame %s\n", savemap );
			return;
		}

		//buffer = Hunk_AllocateTempMemory(size);
		FS_ReadFile( savemap, (void **)&buffer );

		// the mapname is at the very start of the savegame file
		savegameTime = *( int * )( buffer + sizeof( int ) + MAX_QPATH );

		if ( savegameTime >= 0 ) {
			svs.time = savegameTime;
		}

		Hunk_FreeTempMemory( buffer );
	}
	// done.

	// toggle the server bit so clients can detect that a
	// map_restart has happened
	svs.snapFlagServerBit ^= SNAPFLAG_SERVERCOUNT;

	// generate a new serverid	
	// TTimo - don't update restartedserverId there, otherwise we won't deal correctly with multiple map_restart
	sv.serverId = com_frameTime;
	Cvar_Set( "sv_serverid", va("%i", sv.serverId ) );

	// if a map_restart occurs while a client is changing maps, we need
	// to give them the correct time so that when they finish loading
	// they don't violate the backwards time check in cl_cgame.c
	for (i=0 ; i<sv_maxclients->integer ; i++) {
		if (svs.clients[i].state == CS_PRIMED) {
			svs.clients[i].oldServerTime = sv.restartTime;
		}
	}

	// reset all the vm data in place without changing memory allocation
	// note that we do NOT set sv.state = SS_LOADING, so configstrings that
	// had been changed from their default values will generate broadcast updates
	sv.state = SS_LOADING;
	sv.restarting = qtrue;

	Cvar_Set( "sv_serverRestarting", "1" );

	SV_RestartGameProgs();

	// run a few frames to allow everything to settle
	for ( i = 0; i < GAME_INIT_FRAMES; i++ ) {
		VM_Call( gvm, GAME_RUN_FRAME, sv.time );
		sv.time += FRAMETIME;
		svs.time += FRAMETIME;
	}

	sv.state = SS_GAME;
	sv.restarting = qfalse;

	// connect and begin all the clients
	for (i=0 ; i<sv_maxclients->integer ; i++) {
		client = &svs.clients[i];

		// send the new gamestate to all connected clients
		if ( client->state < CS_CONNECTED) {
			continue;
		}

		if ( client->netchan.remoteAddress.type == NA_BOT ) {
			if ( SV_GameIsSinglePlayer() || SV_GameIsCoop() ) {
				continue;   // dont carry across bots in single player
			}
			isBot = qtrue;
		} else {
			isBot = qfalse;
		}

		// add the map_restart command
		SV_AddServerCommand( client, "map_restart\n" );

		// connect the client again, without the firstTime flag
		denied = GVM_ArgPtr( VM_Call( gvm, GAME_CLIENT_CONNECT, i, qfalse, isBot ) );
		if ( denied ) {
			// this generally shouldn't happen, because the client
			// was connected before the level change
			SV_DropClient( client, denied );
			if ( ( !SV_GameIsSinglePlayer() ) || ( !isBot ) ) {
				Com_Printf( "SV_MapRestart_f(%d): dropped client %i - denied!\n", delay, i ); // bk010125
			}
			continue;
		}

		if ( client->state == CS_ACTIVE )
			SV_ClientEnterWorld( client, &client->lastUsercmd );
		else {
			// If we don't reset client->lastUsercmd and are restarting during map load,
			// the client will hang because we'll use the last Usercmd from the previous map,
			// which is wrong obviously.
			SV_ClientEnterWorld( client, NULL );
		}
	}	
	// run another frame to allow things to look at all the players
	VM_Call (gvm, GAME_RUN_FRAME, sv.time);
	sv.time += FRAMETIME;
	svs.time += FRAMETIME;

	Cvar_Set( "sv_serverRestarting", "0" );
}

/*
=================
SV_LoadGame_f
=================
*/
void    SV_LoadGame_f( void ) {
	char filename[MAX_QPATH], mapname[MAX_QPATH], savedir[MAX_QPATH];
	byte *buffer;
	int size;
	const char *cl_profileStr = Cvar_VariableString( "cl_profile" );

	// dont allow command if another loadgame is pending
	if ( Cvar_VariableIntegerValue( "savegame_loading" ) ) {
		return;
	}
	if ( sv_reloading->integer ) {
		// (SA) disabling
//	if(sv_reloading->integer && sv_reloading->integer != RELOAD_FAILED )	// game is in 'reload' mode, don't allow starting new maps yet.
		return;
	}

	Q_strncpyz( filename, Cmd_Argv( 1 ), sizeof( filename ) );
	if ( !filename[0] ) {
		Com_Printf( "You must specify a savegame to load\n" );
		return;
	}

	if ( com_gameInfo.usesProfiles && cl_profileStr[0] ) {
		Com_sprintf( savedir, sizeof( savedir ), "profiles/%s/save/", cl_profileStr );
	} else {
		Q_strncpyz( savedir, "save/", sizeof( savedir ) );
	}

	/*if ( Q_strncmp( filename, "save/", 5 ) && Q_strncmp( filename, "save\\", 5 ) ) {
		Q_strncpyz( filename, va("save/%s", filename), sizeof( filename ) );
	}*/

	// go through a va to avoid vsnprintf call with same source and target
	Q_strncpyz( filename, va( "%s%s", savedir, filename ), sizeof( filename ) );

	// enforce .sav extension
	if ( !strstr( filename, "." ) || Q_strncmp( strstr( filename, "." ) + 1, "sav", 3 ) ) {
		Q_strcat( filename, sizeof( filename ), ".sav" );
	}
	// use '/' instead of '\\' for directories
	while ( strstr( filename, "\\" ) ) {
		*(char *)strstr( filename, "\\" ) = '/';
	}

	size = FS_ReadFile( filename, NULL );
	if ( size < 0 ) {
		Com_Printf( "Can't find savegame %s\n", filename );
		return;
	}

	//buffer = Hunk_AllocateTempMemory(size);
	FS_ReadFile( filename, (void **)&buffer );

	// read the mapname, if it is the same as the current map, then do a fast load
	Com_sprintf( mapname, sizeof( mapname ), (const char*)( buffer + sizeof( int ) ) );

	if ( com_sv_running->integer && ( com_frameTime != sv.serverId ) ) {
		// check mapname
		if ( !Q_stricmp( mapname, sv_mapname->string ) ) {    // same

			if ( Q_stricmp( filename, va( "%scurrent.sav",savedir ) ) != 0 ) {
				// copy it to the current savegame file
				FS_WriteFile( va( "%scurrent.sav",savedir ), buffer, size );
			}

			Hunk_FreeTempMemory( buffer );

			Cvar_Set( "savegame_loading", "2" );  // 2 means it's a restart, so stop rendering until we are loaded
			// set the filename
			Cvar_Set( "savegame_filename", filename );
			// quick-restart the server
			SV_MapRestart_f();  // savegame will be loaded after restart

			return;
		}
	}

	Hunk_FreeTempMemory( buffer );

	// otherwise, do a slow load
	if ( Cvar_VariableIntegerValue( "sv_cheats" ) ) {
		Cbuf_ExecuteText( EXEC_APPEND, va( "spdevmap %s", filename ) );
	} else {    // no cheats
		Cbuf_ExecuteText( EXEC_APPEND, va( "spmap %s", filename ) );
	}
}

//===============================================================

/*
==================
SV_Kick_f

Kick a user off of the server  FIXME: move to game
// fretn: done
==================
*/
/*
static void SV_Kick_f( void ) {
	client_t	*cl;
	int			i;
	int			timeout = -1;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() < 2 || Cmd_Argc() > 3 ) {
		Com_Printf ("Usage: kick <player name> [timeout]\n");
		return;
	}

	if( Cmd_Argc() == 3 ) {
		timeout = atoi( Cmd_Argv( 2 ) );
	} else {
		timeout = 300;
	}

	cl = SV_GetPlayerByName();
	if ( !cl ) {
		if ( !Q_stricmp(Cmd_Argv(1), "all") ) {
			for( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
				if ( !cl->state ) {
					continue;
				}
				if( cl->netchan.remoteAddress.type == NA_LOOPBACK ) {
					continue;
				}
				SV_DropClient( cl, "player kicked" ); // JPW NERVE to match front menu message
				if( timeout != -1 ) {
					SV_TempBanNetAddress( cl->netchan.remoteAddress, timeout );
				}
				cl->lastPacketTime = svs.time;	// in case there is a funny zombie
			}
		} else if ( !Q_stricmp(Cmd_Argv(1), "allbots") ) {
			for ( i=0, cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++ ) {
				if ( !cl->state ) {
					continue;
				}
				if( cl->netchan.remoteAddress.type != NA_BOT ) {
					continue;
				}
				SV_DropClient( cl, "was kicked" );
				if( timeout != -1 ) {
					SV_TempBanNetAddress( cl->netchan.remoteAddress, timeout );
				}
				cl->lastPacketTime = svs.time;	// in case there is a funny zombie
			}
		}
		return;
	}
	if( cl->netchan.remoteAddress.type == NA_LOOPBACK ) {
		SV_SendServerCommand(NULL, "print \"%s\"", "Cannot kick host player\n");
		return;
	}

	SV_DropClient( cl, "player kicked" ); // JPW NERVE to match front menu message
	if( timeout != -1 ) {
		SV_TempBanNetAddress( cl->netchan.remoteAddress, timeout );
	}
	cl->lastPacketTime = svs.time;	// in case there is a funny zombie
}
*/

#ifdef USE_BANS
/*
==================
SV_RehashBans_f

Load saved bans from file.
==================
*/
static void SV_RehashBans_f(void)
{
	int index, filelen;
	fileHandle_t readfrom;
	char *textbuf, *curpos, *maskpos, *newlinepos, *endpos;
	char filepath[MAX_QPATH];
	
	// make sure server is running
	if ( !com_sv_running->integer ) {
		return;
	}
	
	serverBansCount = 0;
	
	if(!sv_banFile->string || !*sv_banFile->string)
		return;

	Com_sprintf(filepath, sizeof(filepath), "%s/%s", FS_GetCurrentGameDir(), sv_banFile->string);

	if((filelen = FS_SV_FOpenFileRead(filepath, &readfrom)) >= 0)
	{
		if(filelen < 2)
		{
			// Don't bother if file is too short.
			FS_FCloseFile(readfrom);
			return;
		}

		curpos = textbuf = Z_Malloc(filelen);
		
		filelen = FS_Read(textbuf, filelen, readfrom);
		FS_FCloseFile(readfrom);
		
		endpos = textbuf + filelen;
		
		for(index = 0; index < SERVER_MAXBANS && curpos + 2 < endpos; index++)
		{
			// find the end of the address string
			for(maskpos = curpos + 2; maskpos < endpos && *maskpos != ' '; maskpos++);
			
			if(maskpos + 1 >= endpos)
				break;

			*maskpos = '\0';
			maskpos++;
			
			// find the end of the subnet specifier
			for(newlinepos = maskpos; newlinepos < endpos && *newlinepos != '\n'; newlinepos++);
			
			if(newlinepos >= endpos)
				break;
			
			*newlinepos = '\0';
			
			if(NET_StringToAdr(curpos + 2, &serverBans[index].ip, NA_UNSPEC))
			{
				serverBans[index].isexception = (curpos[0] != '0');
				serverBans[index].subnet = atoi(maskpos);
				
				if(serverBans[index].ip.type == NA_IP &&
				   (serverBans[index].subnet < 1 || serverBans[index].subnet > 32))
				{
					serverBans[index].subnet = 32;
				}
				else if(serverBans[index].ip.type == NA_IP6 &&
					(serverBans[index].subnet < 1 || serverBans[index].subnet > 128))
				{
					serverBans[index].subnet = 128;
				}
			}
			
			curpos = newlinepos + 1;
		}
			
		serverBansCount = index;
		
		Z_Free(textbuf);
	}
}

/*
==================
SV_WriteBans

Save bans to file.
==================
*/
static void SV_WriteBans(void)
{
	int index;
	fileHandle_t writeto;
	char filepath[MAX_QPATH];
	
	if(!sv_banFile->string || !*sv_banFile->string)
		return;
	
	Com_sprintf(filepath, sizeof(filepath), "%s/%s", FS_GetCurrentGameDir(), sv_banFile->string);

	if((writeto = FS_SV_FOpenFileWrite(filepath)))
	{
		char writebuf[128];
		serverBan_t *curban;
		
		for(index = 0; index < serverBansCount; index++)
		{
			curban = &serverBans[index];
			
			Com_sprintf(writebuf, sizeof(writebuf), "%d %s %d\n",
				    curban->isexception, NET_AdrToString(&curban->ip), curban->subnet);
			FS_Write(writebuf, strlen(writebuf), writeto);
		}

		FS_FCloseFile(writeto);
	}
}

/*
==================
SV_DelBanEntryFromList

Remove a ban or an exception from the list.
==================
*/

static qboolean SV_DelBanEntryFromList(int index)
{
	if(index == serverBansCount - 1)
		serverBansCount--;
	else if(index < ARRAY_LEN(serverBans) - 1)
	{
		memmove(serverBans + index, serverBans + index + 1, (serverBansCount - index - 1) * sizeof(*serverBans));
		serverBansCount--;
	}
	else
		return qtrue;

	return qfalse;
}

/*
==================
SV_ParseCIDRNotation

Parse a CIDR notation type string and return a netadr_t and suffix by reference
==================
*/

static qboolean SV_ParseCIDRNotation(netadr_t *dest, int *mask, char *adrstr)
{
	char *suffix;
	
	suffix = strchr(adrstr, '/');
	if(suffix)
	{
		*suffix = '\0';
		suffix++;
	}

	if(!NET_StringToAdr(adrstr, dest, NA_UNSPEC))
		return qtrue;

	if(suffix)
	{
		*mask = atoi(suffix);
		
		if(dest->type == NA_IP)
		{
			if(*mask < 1 || *mask > 32)
				*mask = 32;
		}
		else
		{
			if(*mask < 1 || *mask > 128)
				*mask = 128;
		}
	}
	else if(dest->type == NA_IP)
		*mask = 32;
	else
		*mask = 128;
	
	return qfalse;
}

/*
==================
SV_AddBanToList

Ban a user from being able to play on this server based on his ip address.
==================
*/

static void SV_AddBanToList(qboolean isexception)
{
	char *banstring;
	char addy2[NET_ADDRSTRMAXLEN];
	netadr_t ip;
	int index, argc, mask;
	serverBan_t *curban;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	argc = Cmd_Argc();
	
	if(argc < 2 || argc > 3)
	{
		Com_Printf ("Usage: %s (ip[/subnet] | clientnum [subnet])\n", Cmd_Argv(0));
		return;
	}

	if(serverBansCount >= ARRAY_LEN(serverBans))
	{
		Com_Printf ("Error: Maximum number of bans/exceptions exceeded.\n");
		return;
	}

	banstring = Cmd_Argv(1);
	
	if(strchr(banstring, '.') || strchr(banstring, ':'))
	{
		// This is an ip address, not a client num.
		
		if(SV_ParseCIDRNotation(&ip, &mask, banstring))
		{
			Com_Printf("Error: Invalid address %s\n", banstring);
			return;
		}
	}
	else
	{
		client_t *cl;
		
		// client num.
		
		cl = SV_GetPlayerByNum();

		if(!cl)
		{
			Com_Printf("Error: Playernum %s does not exist.\n", Cmd_Argv(1));
			return;
		}
		
		ip = cl->netchan.remoteAddress;
		
		if(argc == 3)
		{
			mask = atoi(Cmd_Argv(2));
			
			if(ip.type == NA_IP)
			{
				if(mask < 1 || mask > 32)
					mask = 32;
			}
			else
			{
				if(mask < 1 || mask > 128)
					mask = 128;
			}
		}
		else
			mask = (ip.type == NA_IP6) ? 128 : 32;
	}

	if(ip.type != NA_IP && ip.type != NA_IP6)
	{
		Com_Printf("Error: Can ban players connected via the internet only.\n");
		return;
	}

	// first check whether a conflicting ban exists that would supersede the new one.
	for(index = 0; index < serverBansCount; index++)
	{
		curban = &serverBans[index];
		
		if(curban->subnet <= mask)
		{
			if((curban->isexception || !isexception) && NET_CompareBaseAdrMask(&curban->ip, ip, &curban->subnet))
			{
				Q_strncpyz(addy2, NET_AdrToString(&ip), sizeof(addy2));
				
				Com_Printf("Error: %s %s/%d supersedes %s %s/%d\n", curban->isexception ? "Exception" : "Ban",
					   NET_AdrToString(&curban->ip), curban->subnet,
					   isexception ? "exception" : "ban", addy2, mask);
				return;
			}
		}
		if(curban->subnet >= mask)
		{
			if(!curban->isexception && isexception && NET_CompareBaseAdrMask(&curban->ip, &ip, mask))
			{
				Q_strncpyz(addy2, NET_AdrToString(&curban->ip), sizeof(addy2));
			
				Com_Printf("Error: %s %s/%d supersedes already existing %s %s/%d\n", isexception ? "Exception" : "Ban",
					   NET_AdrToString(&ip), mask,
					   curban->isexception ? "exception" : "ban", addy2, curban->subnet);
				return;
			}
		}
	}

	// now delete bans that are superseded by the new one
	index = 0;
	while(index < serverBansCount)
	{
		curban = &serverBans[index];
		
		if(curban->subnet > mask && (!curban->isexception || isexception) && NET_CompareBaseAdrMask(&curban->ip, &ip, mask))
			SV_DelBanEntryFromList(index);
		else
			index++;
	}

	serverBans[serverBansCount].ip = ip;
	serverBans[serverBansCount].subnet = mask;
	serverBans[serverBansCount].isexception = isexception;
	
	serverBansCount++;
	
	SV_WriteBans();

	Com_Printf("Added %s: %s/%d\n", isexception ? "ban exception" : "ban",
		   NET_AdrToString(&ip), mask);
}

/*
==================
SV_DelBanFromList

Remove a ban or an exception from the list.
==================
*/

static void SV_DelBanFromList(qboolean isexception)
{
	int index, count = 0, todel, mask;
	netadr_t ip;
	char *banstring;
	
	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}
	
	if(Cmd_Argc() != 2)
	{
		Com_Printf ("Usage: %s (ip[/subnet] | num)\n", Cmd_Argv(0));
		return;
	}

	banstring = Cmd_Argv(1);
	
	if(strchr(banstring, '.') || strchr(banstring, ':'))
	{
		serverBan_t *curban;
		
		if(SV_ParseCIDRNotation(&ip, &mask, banstring))
		{
			Com_Printf("Error: Invalid address %s\n", banstring);
			return;
		}
		
		index = 0;
		
		while(index < serverBansCount)
		{
			curban = &serverBans[index];
			
			if(curban->isexception == isexception		&&
			   curban->subnet >= mask 			&&
			   NET_CompareBaseAdrMask(&curban->ip, &ip, mask))
			{
				Com_Printf("Deleting %s %s/%d\n",
					   isexception ? "exception" : "ban",
					   NET_AdrToString(&curban->ip), curban->subnet);
					   
				SV_DelBanEntryFromList(index);
			}
			else
				index++;
		}
	}
	else
	{
		todel = atoi(Cmd_Argv(1));

		if(todel < 1 || todel > serverBansCount)
		{
			Com_Printf("Error: Invalid ban number given\n");
			return;
		}
	
		for(index = 0; index < serverBansCount; index++)
		{
			if(serverBans[index].isexception == isexception)
			{
				count++;
			
				if(count == todel)
				{
					Com_Printf("Deleting %s %s/%d\n",
					   isexception ? "exception" : "ban",
					   NET_AdrToString(&serverBans[index].ip), serverBans[index].subnet);

					SV_DelBanEntryFromList(index);

					break;
				}
			}
		}
	}
	
	SV_WriteBans();
}


/*
==================
SV_ListBans_f

List all bans and exceptions on console
==================
*/

static void SV_ListBans_f(void)
{
	int index, count;
	serverBan_t *ban;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}
	
	// List all bans
	for(index = count = 0; index < serverBansCount; index++)
	{
		ban = &serverBans[index];
		if(!ban->isexception)
		{
			count++;

			Com_Printf("Ban #%d: %s/%d\n", count,
				    NET_AdrToString(&ban->ip), ban->subnet);
		}
	}
	// List all exceptions
	for(index = count = 0; index < serverBansCount; index++)
	{
		ban = &serverBans[index];
		if(ban->isexception)
		{
			count++;

			Com_Printf("Except #%d: %s/%d\n", count,
				    NET_AdrToString(&ban->ip), ban->subnet);
		}
	}
}

/*
==================
SV_FlushBans_f

Delete all bans and exceptions.
==================
*/

static void SV_FlushBans_f(void)
{
	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	serverBansCount = 0;
	
	// empty the ban file.
	SV_WriteBans();
	
	Com_Printf("All bans and exceptions have been deleted.\n");
}

static void SV_BanAddr_f(void)
{
	SV_AddBanToList(qfalse);
}

static void SV_ExceptAddr_f(void)
{
	SV_AddBanToList(qtrue);
}

static void SV_BanDel_f(void)
{
	SV_DelBanFromList(qfalse);
}

static void SV_ExceptDel_f(void)
{
	SV_DelBanFromList(qtrue);
}

#endif // USE_BANS

/*
==================
==================
*/
void SV_TempBanNetAddress( const netadr_t *address, int length ) {
	int i;
	int oldesttime = 0;
	int oldest = -1;

	for ( i = 0; i < MAX_TEMPBAN_ADDRESSES; i++ ) {
		if ( !svs.tempBanAddresses[ i ].endtime || svs.tempBanAddresses[ i ].endtime < svs.time ) {
			// found a free slot
			svs.tempBanAddresses[ i ].adr       = *address;
			svs.tempBanAddresses[ i ].endtime   = svs.time + ( length * 1000 );

			return;
		} else {
			if ( oldest == -1 || oldesttime > svs.tempBanAddresses[ i ].endtime ) {
				oldesttime  = svs.tempBanAddresses[ i ].endtime;
				oldest      = i;
			}
		}
	}

	svs.tempBanAddresses[ oldest ].adr      = *address;
	svs.tempBanAddresses[ oldest ].endtime  = svs.time + length;
}

qboolean SV_TempBanIsBanned( const netadr_t *address ) {
	int i;

	for ( i = 0; i < MAX_TEMPBAN_ADDRESSES; i++ ) {
		if ( svs.tempBanAddresses[ i ].endtime && svs.tempBanAddresses[ i ].endtime > svs.time ) {
			if ( NET_CompareAdr( address, &svs.tempBanAddresses[ i ].adr ) ) {
				return qtrue;
			}
		}
	}

	return qfalse;
}

/*
==================
SV_KickNum_f

Kick a user off of the server  FIXME: move to game
*DONE*
==================
*/
/*
static void SV_KickNum_f( void ) {
	client_t	*cl;
	int timeout = -1;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() < 2 || Cmd_Argc() > 3 ) {
		Com_Printf ("Usage: kicknum <client number> [timeout]\n");
		return;
	}

	if( Cmd_Argc() == 3 ) {
		timeout = atoi( Cmd_Argv( 2 ) );
	} else {
		timeout = 300;
	}

	cl = SV_GetPlayerByNum();
	if ( !cl ) {
		return;
	}
	if( cl->netchan.remoteAddress.type == NA_LOOPBACK ) {
		SV_SendServerCommand(NULL, "print \"%s\"", "Cannot kick host player\n");
		return;
	}

	SV_DropClient( cl, "player kicked" );
	if( timeout != -1 ) {
		SV_TempBanNetAddress( cl->netchan.remoteAddress, timeout );
	}
	cl->lastPacketTime = svs.time;	// in case there is a funny zombie
}
*/




/*
** SV_Strlen -- skips color escape codes
*/
static int SV_Strlen( const char *str ) {
	const char *s = str;
	int count = 0;

	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
		} else {
			count++;
			s++;
		}
	}

	return count;
}


/*
================
SV_Status_f
================
*/
static void SV_Status_f( void ) {
	int			i, j, l;
	client_t	*cl;
	playerState_t	*ps;
	const char		*s;
	int			ping;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	Com_Printf ("map: %s\n", sv_mapname->string );

	Com_Printf ("cl score ping name            address                                 rate \n");
	Com_Printf ("-- ----- ---- --------------- --------------------------------------- -----\n");
	for (i=0,cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++)
	{
		if (!cl->state)
			continue;
		Com_Printf ("%2i ", i);
		ps = SV_GameClientNum( i );
		Com_Printf ("%5i ", ps->persistant[PERS_SCORE]);

		if (cl->state == CS_CONNECTED)
			Com_Printf ("CON ");
		else if (cl->state == CS_ZOMBIE)
			Com_Printf ("ZMB ");
		else
		{
			ping = cl->ping < 9999 ? cl->ping : 9999;
			Com_Printf ("%4i ", ping);
		}

		Com_Printf ("%s", cl->name);
		
		l = 16 - SV_Strlen(cl->name);
		j = 0;
		
		do
		{
			Com_Printf (" ");
			j++;
		} while(j < l);


		// TTimo adding a ^7 to reset the color
		s = NET_AdrToString( &cl->netchan.remoteAddress );
		Com_Printf ("^7%s", s);
		l = 39 - strlen(s);
		j = 0;
		
		do
		{
			Com_Printf(" ");
			j++;
		} while(j < l);
		
		Com_Printf (" %5i", cl->rate);

		Com_Printf ("\n");
	}
	Com_Printf ("\n");
}


/*
==================
SV_ConSay_f
==================
*/
static void SV_ConSay_f( void ) {
	char	*p;
	char	text[1024];

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc () < 2 ) {
		return;
	}

	strcpy( text, "console: " );
	p = Cmd_ArgsFrom( 1 );

	if ( strlen( p ) > 1000 ) {
		return;
	}

	if ( *p == '"' ) {
		p++;
		p[strlen(p)-1] = '\0';
	}

	strcat( text, p );

	SV_SendServerCommand( NULL, "chat \"%s\"", text );
}


/*
==================
SV_ConTell_f
==================
*/
static void SV_ConTell_f( void ) {
	char	*p;
	char	text[1024];
	client_t	*cl;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() < 3 ) {
		Com_Printf( "Usage: tell <client number> <text>\n" );
		return;
	}

	cl = SV_GetPlayerByNum();
	if ( !cl ) {
		return;
	}

	strcpy( text, "console_tell: " );
	p = Cmd_ArgsFrom( 2 );

	if ( strlen( p ) > 1000 ) {
		return;
	}

	if ( *p == '"' ) {
		p++;
		p[strlen(p)-1] = '\0';
	}

	strcat( text, p );

	Com_Printf( "%s\n", text );
	SV_SendServerCommand( cl, "chat \"%s\"", text );
}


/*
==================
SV_Heartbeat_f

Also called by SV_DropClient, SV_DirectConnect, and SV_SpawnServer
==================
*/
void SV_Heartbeat_f( void ) {
	svs.nextHeartbeatTime = -9999999;
}


/*
===========
SV_Serverinfo_f

Examine the serverinfo string
===========
*/
static void SV_Serverinfo_f( void ) {
	const char *info;
	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}
	Com_Printf ("Server info settings:\n");
	info = sv.configstrings[ CS_SERVERINFO ];
	if ( info ) {
		Info_Print( info );
	}
}


/*
===========
SV_Systeminfo_f

Examine the systeminfo string
===========
*/
static void SV_Systeminfo_f( void ) {
	const char *info;
	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}
	Com_Printf( "System info settings:\n" );
	info = sv.configstrings[ CS_SYSTEMINFO ];
	if ( info ) {
		Info_Print( info );
	}
}


/*
===========
SV_DumpUser_f

Examine all a users info strings
===========
*/
static void SV_DumpUser_f( void ) {
	client_t	*cl;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("Usage: dumpuser <userid>\n");
		return;
	}

	cl = SV_GetPlayerByHandle();
	if ( !cl ) {
		return;
	}

	Com_Printf( "userinfo\n" );
	Com_Printf( "--------\n" );
	Info_Print( cl->userinfo );
}


/*
=================
SV_KillServer
=================
*/
static void SV_KillServer_f( void ) {
	SV_Shutdown( "killserver" );
}

/*
=================
SV_GameCompleteStatus_f

NERVE - SMF
=================
*/
void SV_GameCompleteStatus_f( void ) {
	SV_MasterGameCompleteStatus();
}

//===========================================================

/*
==================
SV_CompleteMapName
==================
*/
void SV_CompleteMapName( char *args, int argNum ) {
	if ( argNum == 2 ) 	{
		if ( sv_pure->integer ) {
			Field_CompleteFilename( "maps", "bsp", qtrue, FS_MATCH_PK3s | FS_MATCH_STICK );
		} else {
			Field_CompleteFilename( "maps", "bsp", qtrue, FS_MATCH_ANY | FS_MATCH_STICK );
		}
	}
}


/*
==================
SV_AddOperatorCommands
==================
*/
void SV_AddOperatorCommands( void ) {
	static qboolean	initialized;

	if ( initialized ) {
		return;
	}
	initialized = qtrue;

	Cmd_AddCommand( "heartbeat", SV_Heartbeat_f );
// fretn - moved to qagame
	/*Cmd_AddCommand ("kick", SV_Kick_f);
	Cmd_AddCommand ("clientkick", SV_KickNum_f);*/
	Cmd_AddCommand( "status", SV_Status_f );
	Cmd_AddCommand( "dumpuser", SV_DumpUser_f );
	Cmd_AddCommand( "map_restart", SV_MapRestart_f );
#ifdef MSG_FIELDINFO_SORT_DEBUG
	Cmd_AddCommand( "fieldinfo", SV_FieldInfo_f );
#endif
	Cmd_AddCommand( "sectorlist", SV_SectorList_f );
	Cmd_AddCommand( "map", SV_Map_f );
	Cmd_SetCommandCompletionFunc( "map", SV_CompleteMapName );
	Cmd_AddCommand( "gameCompleteStatus", SV_GameCompleteStatus_f );      // NERVE - SMF
	Cmd_AddCommand( "devmap", SV_Map_f );
	Cmd_SetCommandCompletionFunc( "devmap", SV_CompleteMapName );
	Cmd_AddCommand( "spmap", SV_Map_f );
	Cmd_SetCommandCompletionFunc( "spmap", SV_CompleteMapName );
	Cmd_AddCommand( "spdevmap", SV_Map_f );
	Cmd_SetCommandCompletionFunc( "spdevmap", SV_CompleteMapName );
	Cmd_AddCommand( "loadgame", SV_LoadGame_f );
	Cmd_AddCommand( "killserver", SV_KillServer_f );
#ifdef USE_BANS	
	Cmd_AddCommand("rehashbans", SV_RehashBans_f);
	Cmd_AddCommand("listbans", SV_ListBans_f);
	Cmd_AddCommand("banaddr", SV_BanAddr_f);
	Cmd_AddCommand("exceptaddr", SV_ExceptAddr_f);
	Cmd_AddCommand("bandel", SV_BanDel_f);
	Cmd_AddCommand("exceptdel", SV_ExceptDel_f);
	Cmd_AddCommand("flushbans", SV_FlushBans_f);
#endif
}


/*
==================
SV_RemoveOperatorCommands
==================
*/
void SV_RemoveOperatorCommands( void ) {
#if 0
	// removing these won't let the server start again
	Cmd_RemoveCommand( "heartbeat" );
	Cmd_RemoveCommand( "kick" );
	Cmd_RemoveCommand( "banUser" );
	Cmd_RemoveCommand( "banClient" );
	Cmd_RemoveCommand( "status" );
	Cmd_RemoveCommand( "dumpuser" );
	Cmd_RemoveCommand( "map_restart" );
	Cmd_RemoveCommand( "sectorlist" );
#endif
}


void SV_AddDedicatedCommands( void )
{
	Cmd_AddCommand( "serverinfo", SV_Serverinfo_f );
	Cmd_AddCommand( "systeminfo", SV_Systeminfo_f );
	Cmd_AddCommand( "tell", SV_ConTell_f );
	Cmd_AddCommand( "say", SV_ConSay_f );
}


void SV_RemoveDedicatedCommands( void )
{
	Cmd_RemoveCommand( "serverinfo" );
	Cmd_RemoveCommand( "systeminfo" );
	Cmd_RemoveCommand( "tell" );
	Cmd_RemoveCommand( "say" );
}
