use v5.40;
use blib;

package SDL3 {
    use Affix qw[affix wrap pin callback load_library find_symbol get_last_error_message sizeof];
    use Config;
    use Alien::SDL3;
    #
    $|++;
    #
    use Data::Dump;
    ddx Alien::SDL3->dynamic_libs;
    my ( $sdl3, @etc_libs ) = Alien::SDL3->dynamic_libs;

    #~ warn `nm $sdl3`;
    # SDL_version.h
    #~ pin our ($SDL_MAJOR_VERSION), $sdl3, 'SDL_MAJOR_VERSON', 'int';
    affix $sdl3, 'SDL_GetRevision', '()->*char';
    affix $sdl3, 'SDL_GetVersion',  '()->int';
    #
};
#
use Data::Dump;
ddx $SDL::SDL_MAJOR_VERSON;
ddx SDL3::SDL_GetRevision();
ddx SDL3::SDL_GetVersion();
