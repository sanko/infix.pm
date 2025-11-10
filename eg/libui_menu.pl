#!/usr/bin/perl
use FindBin '$Bin';
use lib '../lib', 'lib';
use blib;
use lib $Bin;
use strict;
use warnings;
use File::Basename qw[dirname];
use FindBin        qw[$Bin];
use lib dirname($Bin);
use LibUI ':all';

# -- Menu Item Callbacks --
sub on_open_clicked {
    my ( $menu_item, $window, $data ) = @_;
    my $filepath = uiOpenFile($window);
    if ($filepath) {
        uiMsgBox( $window, "File Selected", "You chose: $filepath" );
        uiFreeText($filepath);
    }
    else {
        uiMsgBox( $window, "File Selected", "You did not select a file." );
    }
}

sub on_about_clicked {
    my ( $menu_item, $window, $data ) = @_;
    uiMsgBox( $window, "About", "Perl/LibUI Menu Demo\nVersion 1.0" );
}

# Main Program
die "Failed to init LibUI" if LibUI::uiInit( { Size => 0 } );
my $file_menu = uiNewMenu("File");
my $open_item = uiMenuAppendItem( $file_menu, "Open" );

# The window doesn't exist yet, so we'll pass undef for now
# and connect the callback later.
# A better approach is to connect it to a sub that knows how
# to get the main window. For simplicity, we'll keep it as is
# but be aware of this. In this case, the $window parameter passed
# to the callback will be the correct window handle at runtime.
my $save_item = uiMenuAppendItem( $file_menu, "Save" );
uiMenuItemDisable($save_item);
uiMenuAppendSeparator($file_menu);
my $quit_item = uiMenuAppendQuitItem($file_menu);

# 'Help' Menu
my $help_menu  = uiNewMenu("Help");
my $about_item = uiMenuAppendAboutItem($help_menu);
#
# NOW, create the window, telling it to use the menus we just made.
# The last argument '1' tells the window it has a menubar.
#
my $window = uiNewWindow( "Menu and Dialogs Demo", 400, 300, 1 );

# Set the closing callback now that we have a window handle.
uiWindowOnClosing( $window, sub { uiQuit(); 1 }, undef );
#
# Now that both the menu items and the window exist, connect the callbacks.
#
uiMenuItemOnClicked( $open_item,  \&on_open_clicked,  $window );
uiMenuItemOnClicked( $about_item, \&on_about_clicked, $window );

# The Quit item is automatically handled by libui use uiOnShouldQuit() instead.
uiOnShouldQuit( sub { uiControlDestroy($window); uiQuit() }, undef );
#
# Finally, build the rest of the window's content.
#
my $vbox = uiNewVerticalBox();
uiWindowSetChild( $window, $vbox );
uiBoxAppend( $vbox, uiNewLabel("Select an option from the menu."), 0 );

# Show and Run
uiControlShow($window);
uiMain();
uiUninit();
