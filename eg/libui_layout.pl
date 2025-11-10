#!/usr/bin/perl
use FindBin '$Bin';
use lib '../lib', 'lib';
use blib;
use lib $Bin;
use LibUI qw[:all];
use strict;
use warnings;
use File::Basename qw[dirname];
use FindBin        qw[$Bin];
use lib dirname($Bin);
use LibUI ':all';
die "Failed to init LibUI" if LibUI::uiInit( { Size => 0 } );
my $window = uiNewWindow( "Tab and Form Demo", 400, 300, 0 );
uiWindowSetMargined( $window, 1 );
uiWindowOnClosing( $window, sub { uiQuit(); 1; }, undef );

# Create the main tab control
my $tab = uiNewTab();
uiWindowSetChild( $window, $tab );

# -- First Tab: A Data Entry Form --
my $form_vbox = uiNewVerticalBox();
uiBoxSetPadded( $form_vbox, 1 );
uiTabAppend( $tab, "User Profile", $form_vbox );
my $form = uiNewForm();
uiFormSetPadded( $form, 1 );
uiBoxAppend( $form_vbox, $form, 1 );
my $name_entry = uiNewEntry();
uiFormAppend( $form, "Full Name", $name_entry, 0 );
my $email_entry = uiNewEntry();
uiFormAppend( $form, "Email", $email_entry, 0 );
my $password_entry = uiNewPasswordEntry();
uiFormAppend( $form, "Password", $password_entry, 0 );

# -- Second Tab: Options --
my $options_vbox = uiNewVerticalBox();
uiBoxSetPadded( $options_vbox, 1 );
uiTabAppend( $tab, "Settings", $options_vbox );
uiTabSetMargined( $tab, 1, 1 );
my $rb = uiNewRadioButtons();
uiRadioButtonsAppend( $rb, "Option 1" );
uiRadioButtonsAppend( $rb, "Option 2" );
uiRadioButtonsAppend( $rb, "Option 3" );
uiBoxAppend( $options_vbox, $rb,                        0 );
uiBoxAppend( $options_vbox, uiNewHorizontalSeparator(), 0 );
my $checkbox = uiNewCheckbox("Enable feature X");
uiBoxAppend( $options_vbox, $checkbox, 0 );

# Show and Run
uiControlShow($window);
uiMain();
uiUninit();
