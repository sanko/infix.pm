package LibUI v1.0.0 {
    use v5.36;
    use Exporter qw[import];
    use Affix    qw[:all];
    use Alien::libui;
    use Carp qw[croak];
    #
    our @EXPORT_OK = ();

    # Get the path to the shared library from Alien::libui
    my $lib = Alien::libui->dynamic_libs;
    croak "Could not find libui shared library" unless $lib;
    {
        no strict 'refs';

        # Constants and Enums from ui.h
        for my ( $key, $value )(
            uiAlignFill                   => 0,
            uiAlignStart                  => 1,
            uiAlignCenter                 => 2,
            uiAlignEnd                    => 3,
            uiAtLeading                   => 0,
            uiAtTop                       => 1,
            uiAtTrailing                  => 2,
            uiAtBottom                    => 3,
            uiDrawBrushTypeSolid          => 0,
            uiDrawBrushTypeLinearGradient => 1,
            uiDrawBrushTypeRadialGradient => 2,
            uiDrawBrushTypeImage          => 3,
            uiDrawLineCapFlat             => 0,
            uiDrawLineCapRound            => 1,
            uiDrawLineCapSquare           => 2,
            uiDrawLineJoinMiter           => 0,
            uiDrawLineJoinRound           => 1,
            uiDrawLineJoinBevel           => 2,
            uiDrawFillModeWinding         => 0,
            uiDrawFillModeAlternate       => 1,
            uiModifierCtrl                => 1 << 0,
            uiModifierAlt                 => 1 << 1,
            uiModifierShift               => 1 << 2,
            uiModifierSuper               => 1 << 3,
            uiExtKeyEscape                => 1,
            uiExtKeyInsert                => 2,
            uiExtKeyDelete                => 3,
            uiExtKeyHome                  => 4,
            uiExtKeyEnd                   => 5,
            uiExtKeyPageUp                => 6,
            uiExtKeyPageDown              => 7,
            uiExtKeyUp                    => 8,
            uiExtKeyDown                  => 9,
            uiExtKeyLeft                  => 10,
            uiExtKeyRight                 => 11,
            uiExtKeyF1                    => 12,
            uiExtKeyF2                    => 13,
            uiExtKeyF3                    => 14,
            uiExtKeyF4                    => 15,
            uiExtKeyF5                    => 16,
            uiExtKeyF6                    => 17,
            uiExtKeyF7                    => 18,
            uiExtKeyF8                    => 19,
            uiExtKeyF9                    => 20,
            uiExtKeyF10                   => 21,
            uiExtKeyF11                   => 22,
            uiExtKeyF12                   => 23,
            uiExtKeyN0                    => 24,
            uiExtKeyN1                    => 25,
            uiExtKeyN2                    => 26,
            uiExtKeyN3                    => 27,
            uiExtKeyN4                    => 28,
            uiExtKeyN5                    => 29,
            uiExtKeyN6                    => 30,
            uiExtKeyN7                    => 31,
            uiExtKeyN8                    => 32,
            uiExtKeyN9                    => 33,
            uiExtKeyNDot                  => 34,
            uiExtKeyNEnter                => 35,
            uiExtKeyNAdd                  => 36,
            uiExtKeyNSubtract             => 37,
            uiExtKeyNMultiply             => 38,
            uiExtKeyNDivide               => 39,
            uiWindowResizeEdgeLeft        => 0,
            uiWindowResizeEdgeTop         => 1,
            uiWindowResizeEdgeRight       => 2,
            uiWindowResizeEdgeBottom      => 3,
            uiWindowResizeEdgeTopLeft     => 4,
            uiWindowResizeEdgeTopRight    => 5,
            uiWindowResizeEdgeBottomLeft  => 6,
            uiWindowResizeEdgeBottomRight => 7,
            uiTableValueTypeString        => 0,
            uiTableValueTypeImage         => 1,
            uiTableValueTypeInt           => 2,
            uiTableValueTypeColor         => 3
        ) {
            *{$key} = sub () {$value};
            push @EXPORT_OK, "\$$key";
        }
    }

    # Type Definitions
    typedef <<'END_TYPEDEFS';
    # Part 1: Opaque Handle Aliases
    @uiControl = *void;  # Define a single, canonical opaque pointer for all widget handles
    @HeapString = *char; # New type for strings that must be freed by the caller

    # Alias all other widget types to the base uiControl type for consistency.
    @uiWindow = @uiControl;
    @uiButton = @uiControl;
    @uiBox = @uiControl;
    @uiCheckbox = @uiControl;
    @uiEntry = @uiControl;
    @uiLabel = @uiControl;
    @uiTab = @uiControl;
    @uiGroup = @uiControl;
    @uiSpinbox = @uiControl;
    @uiSlider = @uiControl;
    @uiProgressBar = @uiControl;
    @uiSeparator = @uiControl;
    @uiCombobox = @uiControl;
    @uiEditableCombobox = @uiControl;
    @uiRadioButtons = @uiControl;
    @uiDateTimePicker = @uiControl;
    @uiMultilineEntry = @uiControl;
    @uiMenuItem = @uiControl;
    @uiMenu = @uiControl;
    @uiForm = @uiControl;
    @uiGrid = @uiControl;
    @uiImage = @uiControl;
    @uiDrawPath = @uiControl;
    @uiDrawContext = @uiControl;
    @uiArea = @uiControl;
    @uiFontButton = @uiControl;
    @uiColorButton = @uiControl;
    @uiTable = @uiControl;
    @uiTableModel = *void;
    @uiTableValue = *void;

    # Part 2: Forward Declarations for Defined Structs
    @uiAreaHandler;
    @uiDrawMatrix;
    @uiDrawBrush;
    @uiDrawStrokeParams;
    @uiAreaKeyEvent;
    @uiAreaMouseEvent;
    @uiAreaDrawParams;
    @uiDrawBrushGradientStop;
    @uiTableModelHandler;
    @uiTableParams;

    # Part 3: Full Struct Definitions
    @uiInitOptions = { Size: size_t };

    # New type for passing raw SV* pointers through C
    @SV = *void;

    @uiDrawMatrix = {
        M11: double, M12: double,
        M21: double, M22: double,
        M31: double, M32: double
    };

    @uiDrawBrushGradientStop = { Pos: double, R: double, G: double, B: double, A: double };

    @uiDrawBrush = {
        Type: int,
        R: double, G: double, B: double, A: double,
        X0: double, Y0: double, X1: double, Y1: double,
        OuterRadius: double,
        Stops: *@uiDrawBrushGradientStop,
        NumStops: size_t
    };

    @uiDrawStrokeParams = {
        Cap: int,
        Join: int,
        MiterLimit: double,
        Dashes: *double,
        NumDashes: size_t,
        DashPhase: double,
        Thickness: double
    };

    @uiAreaKeyEvent = {
        Key: char,
        ExtKey: int,
        Modifier: int,
        Modifiers: int,
        Up: int
    };

    @uiAreaMouseEvent = {
        X: double, Y: double,
        AreaWidth: double, AreaHeight: double,
        Down: int, Up: int,
        Count: int,
        Modifiers: int,
        Held1To64: uint64
    };

    @uiAreaDrawParams = {
        Context: @uiDrawContext,
        AreaWidth: double,
        AreaHeight: double,
        ClipX: double,
        ClipY: double,
        ClipWidth: double,
        ClipHeight: double
    };

    @uiAreaHandler = {
        Draw:           *((*@uiAreaHandler, @uiArea, *@uiAreaDrawParams)->void),
        MouseEvent:     *((*@uiAreaHandler, @uiArea, *@uiAreaMouseEvent)->void),
        MouseCrossed:   *((*@uiAreaHandler, @uiArea, int)->void),
        DragBroken:     *((*@uiAreaHandler, @uiArea)->void),
        KeyEvent:       *((*@uiAreaHandler, @uiArea, *@uiAreaKeyEvent)->int)
    };


    # Table structs needed for the table example
    @uiTableModelHandler = {
        NumColumns:   *((*@uiTableModelHandler, @uiTableModel)->int),
        ColumnType:   *((*@uiTableModelHandler, @uiTableModel, int)->int),
        NumRows:      *((*@uiTableModelHandler, @uiTableModel)->int),
        CellValue:    *((*@uiTableModelHandler, @uiTableModel, int, int)->@uiTableValue),
        SetCellValue: *((*@uiTableModelHandler, @uiTableModel, int, int, *@uiTableValue)->void)
    };
    @uiTableParams = { Model: @uiTableModel, RowBackgroundColorModelColumn: int };
END_TYPEDEFS
    for my ( $name, $sig )(

        # Main entry points
        uiInit          => '(options: *@uiInitOptions)->*char',
        uiUninit        => '()->void',
        uiFreeInitError => '(err: *char)->void',
        uiMain          => '()->void',
        uiMainSteps     => '()->void',
        uiMainStep      => '(wait: int)->int',
        uiQuit          => '()->void',
        uiQueueMain     => '(cb: *((@SV)->void), data: @SV)->void',
        uiTimer         => '(ms: int, cb: *((@SV)->int), data: @SV)->void',
        uiOnShouldQuit  => '(cb: *((@SV)->int), data: @SV)->void',
        uiFreeText      => '(text: @HeapString)->void',

        # Generic Control functions
        uiControlDestroy   => '(@uiControl)->void',
        uiControlHandle    => '(@uiControl)->*void',
        uiControlParent    => '(@uiControl)->@uiControl',
        uiControlSetParent => '(@uiControl, @uiControl)->void',
        uiControlToplevel  => '(@uiControl)->int',
        uiControlVisible   => '(@uiControl)->int',
        uiControlShow      => '(@uiControl)->void',
        uiControlHide      => '(@uiControl)->void',
        uiControlEnabled   => '(@uiControl)->int',
        uiControlEnable    => '(@uiControl)->void',
        uiControlDisable   => '(@uiControl)->void',

        # Window
        uiNewWindow                  => '(title: *char, width: int, height: int, hasMenubar: int)->@uiWindow',
        uiWindowTitle                => '(@uiWindow)->*char',
        uiWindowSetTitle             => '(@uiWindow, *char)->void',
        uiWindowContentSize          => '(@uiWindow, *int, *int)->void',
        uiWindowSetContentSize       => '(@uiWindow, int, int)->void',
        uiWindowFullscreen           => '(@uiWindow)->int',
        uiWindowSetFullscreen        => '(@uiWindow, int)->void',
        uiWindowOnContentSizeChanged => '(@uiWindow, cb: *((@uiWindow, @SV)->void), data: @SV)->void',
        uiWindowOnClosing            => '(@uiWindow, cb: *((@uiWindow, @SV)->int), data: @SV)->void',
        uiWindowOnPositionChanged    => '(@uiWindow, cb: *((@uiWindow, @SV)->void), data: @SV)->void',
        uiWindowBorderless           => '(@uiWindow)->int',
        uiWindowSetBorderless        => '(@uiWindow, int)->void',
        uiWindowChild                => '(@uiWindow)->@uiControl',
        uiWindowSetChild             => '(@uiWindow, @uiControl)->void',
        uiWindowMargined             => '(@uiWindow)->int',
        uiWindowSetMargined          => '(@uiWindow, int)->void',

        # Button
        uiNewButton       => '(*char)->@uiButton',
        uiButtonText      => '(@uiButton)->*char',
        uiButtonSetText   => '(@uiButton, *char)->void',
        uiButtonOnClicked => '(@uiButton, cb: *((@uiButton, @SV)->void), data: @SV)->void',

        # Box
        uiNewHorizontalBox => '()->@uiBox',
        uiNewVerticalBox   => '()->@uiBox',
        uiBoxAppend        => '(@uiBox, @uiControl, int)->void',
        uiBoxDelete        => '(@uiBox, int)->void',
        uiBoxPadded        => '(@uiBox)->int',
        uiBoxSetPadded     => '(@uiBox, int)->void',

        # Checkbox
        uiNewCheckbox        => '(*char)->@uiCheckbox',
        uiCheckboxText       => '(@uiCheckbox)->*char',
        uiCheckboxSetText    => '(@uiCheckbox, *char)->void',
        uiCheckboxOnToggled  => '(@uiCheckbox, cb: *((@uiCheckbox, @SV)->void), data: @SV)->void',
        uiCheckboxChecked    => '(@uiCheckbox)->int',
        uiCheckboxSetChecked => '(@uiCheckbox, int)->void',

        # Entry
        uiNewEntry         => '()->@uiEntry',
        uiNewPasswordEntry => '()->@uiEntry',
        uiNewSearchEntry   => '()->@uiEntry',
        uiEntryText        => '(@uiEntry)->@HeapString',
        uiEntrySetText     => '(@uiEntry, *char)->void',
        uiEntryOnChanged   => '(@uiEntry, cb: * ((@uiEntry, @SV)->void), data: @SV)->void',
        uiEntryReadOnly    => '(@uiEntry)->int',
        uiEntrySetReadOnly => '(@uiEntry, int)->void',

        # Label
        uiNewLabel     => '(*char)->@uiLabel',
        uiLabelText    => '(@uiLabel)->*char',
        uiLabelSetText => '(@uiLabel, *char)->void',

        # Tab
        uiNewTab         => '()->@uiTab',
        uiTabAppend      => '(@uiTab, *char, @uiControl)->void',
        uiTabInsertAt    => '(@uiTab, *char, int, @uiControl)->void',
        uiTabDelete      => '(@uiTab, int)->void',
        uiTabNumPages    => '(@uiTab)->int',
        uiTabMargined    => '(@uiTab, int)->int',
        uiTabSetMargined => '(@uiTab, int, int)->void',

        # Group
        uiNewGroup         => '(*char)->@uiGroup',
        uiGroupTitle       => '(@uiGroup)->*char',
        uiGroupSetTitle    => '(@uiGroup, *char)->void',
        uiGroupSetChild    => '(@uiGroup, @uiControl)->void',
        uiGroupMargined    => '(@uiGroup)->int',
        uiGroupSetMargined => '(@uiGroup, int)->void',

        # Spinbox
        uiNewSpinbox       => '(min: int, max: int)->@uiSpinbox',
        uiSpinboxValue     => '(@uiSpinbox)->int',
        uiSpinboxSetValue  => '(@uiSpinbox, int)->void',
        uiSpinboxOnChanged => '(@uiSpinbox, cb: *((@uiSpinbox, @SV)->void), data: @SV)->void',

        # Slider
        uiNewSlider       => '(min: int, max: int)->@uiSlider',
        uiSliderValue     => '(@uiSlider)->int',
        uiSliderSetValue  => '(@uiSlider, int)->void',
        uiSliderOnChanged => '(@uiSlider, cb: *((@uiSlider, @SV)->void), data: @SV)->void',

        # ProgressBar
        uiNewProgressBar      => '()->@uiProgressBar',
        uiProgressBarValue    => '(@uiProgressBar)->int',
        uiProgressBarSetValue => '(@uiProgressBar, int)->void',

        # Separator
        uiNewHorizontalSeparator => '()->@uiSeparator', uiNewVerticalSeparator => '()->@uiSeparator',

        # Combobox
        uiNewCombobox         => '()->@uiCombobox',
        uiComboboxAppend      => '(@uiCombobox, *char)->void',
        uiComboboxSelected    => '(@uiCombobox)->int',
        uiComboboxSetSelected => '(@uiCombobox, int)->void',
        uiComboboxOnSelected  => '(@uiCombobox, cb: *((@uiCombobox, @SV)->void), data: @SV)->void',

        # Editable Combobox
        uiNewEditableCombobox       => '()->@uiEditableCombobox',
        uiEditableComboboxAppend    => '(@uiEditableCombobox, *char)->void',
        uiEditableComboboxText      => '(@uiEditableCombobox)->*char',
        uiEditableComboboxSetText   => '(@uiEditableCombobox, *char)->void',
        uiEditableComboboxOnChanged => '(@uiEditableCombobox, cb: *((@uiEditableCombobox, @SV)->void), data: @SV)->void',

        # RadioButtons
        uiNewRadioButtons         => '()->@uiRadioButtons',
        uiRadioButtonsAppend      => '(@uiRadioButtons, *char)->void',
        uiRadioButtonsSelected    => '(@uiRadioButtons)->int',
        uiRadioButtonsSetSelected => '(@uiRadioButtons, int)->void',
        uiRadioButtonsOnSelected  => '(@uiRadioButtons, cb: * ((@uiRadioButtons, @SV)->void), data: @SV)->void',

        # DateTimePicker
        uiNewDateTimePicker => '()->@uiDateTimePicker',
        uiNewDatePicker     => '()->@uiDateTimePicker',
        uiNewTimePicker     => '()->@uiDateTimePicker',

        # MultilineEntry
        uiNewMultilineEntry            => '()->@uiMultilineEntry',
        uiNewNonWrappingMultilineEntry => '()->@uiMultilineEntry',
        uiMultilineEntryText           => '(@uiMultilineEntry)->*char',
        uiMultilineEntrySetText        => '(@uiMultilineEntry, *char)->void',
        uiMultilineEntryAppend         => '(@uiMultilineEntry, *char)->void',
        uiMultilineEntryOnChanged      => '(@uiMultilineEntry, cb: * ((@uiMultilineEntry, @SV)->void), data: @SV)->void',
        uiMultilineEntryReadOnly       => '(@uiMultilineEntry)->int',
        uiMultilineEntrySetReadOnly    => '(@uiMultilineEntry, int)->void',

        # Menu
        uiNewMenu                   => '(*char)->@uiMenu',
        uiMenuAppendItem            => '(@uiMenu, *char)->@uiMenuItem',
        uiMenuAppendCheckItem       => '(@uiMenu, *char)->@uiMenuItem',
        uiMenuAppendQuitItem        => '(@uiMenu)->@uiMenuItem',
        uiMenuAppendPreferencesItem => '(@uiMenu)->@uiMenuItem',
        uiMenuAppendAboutItem       => '(@uiMenu)->@uiMenuItem',
        uiMenuAppendSeparator       => '(@uiMenu)->void',

        # MenuItem
        uiMenuItemEnable     => '(@uiMenuItem)->void',
        uiMenuItemDisable    => '(@uiMenuItem)->void',
        uiMenuItemOnClicked  => '(@uiMenuItem, cb: * ((@uiMenuItem, @uiWindow, @SV)->void), data: @SV)->void',
        uiMenuItemChecked    => '(@uiMenuItem)->int',
        uiMenuItemSetChecked => '(@uiMenuItem, int)->void',

        # Form
        uiNewForm       => '()->@uiForm',
        uiFormAppend    => '(@uiForm, *char, @uiControl, int)->void',
        uiFormDelete    => '(@uiForm, int)->void',
        uiFormPadded    => '(@uiForm)->int',
        uiFormSetPadded => '(@uiForm, int)->void',

        # Grid
        uiNewGrid       => '()->@uiGrid',
        uiGridAppend    => '(@uiGrid, @uiControl, int, int, int, int, int, int, int, int)->void',
        uiGridInsertAt  => '(@uiGrid, @uiControl, @uiControl, int, int, int, int, int, int, int)->void',
        uiGridPadded    => '(@uiGrid)->int',
        uiGridSetPadded => '(@uiGrid, int)->void',

        # Image
        uiNewImage    => '(width: double, height: double)->@uiImage',
        uiFreeImage   => '(@uiImage)->void',
        uiImageAppend => '(@uiImage, *void, int, int, int)->void',

        # Drawing
        uiDrawNewPath              => '(fillMode: int)->@uiDrawPath',
        uiDrawFreePath             => '(@uiDrawPath)->void',
        uiDrawPathNewFigure        => '(@uiDrawPath, double, double)->void',
        uiDrawPathNewFigureWithArc => '(@uiDrawPath, double, double, double, double, double, int)->void',
        uiDrawPathLineTo           => '(@uiDrawPath, double, double)->void',
        uiDrawPathArcTo            => '(@uiDrawPath, double, double, double, double, int)->void',
        uiDrawPathBezierTo         => '(@uiDrawPath, double, double, double, double, double, double)->void',
        uiDrawPathCloseFigure      => '(@uiDrawPath)->void',
        uiDrawPathAddRectangle     => '(@uiDrawPath, double, double, double, double)->void',
        uiDrawPathEnd              => '(@uiDrawPath)->void',
        uiDrawStroke               => '(@uiDrawContext, @uiDrawPath, *@uiDrawBrush, *@uiDrawStrokeParams)->void',
        uiDrawFill                 => '(@uiDrawContext, @uiDrawPath, *@uiDrawBrush)->void',
        uiDrawMatrixSetIdentity    => '(*@uiDrawMatrix)->void',
        uiDrawMatrixTranslate      => '(*@uiDrawMatrix, double, double)->void',
        uiDrawMatrixScale          => '(*@uiDrawMatrix, double, double, double, double)->void',
        uiDrawMatrixRotate         => '(*@uiDrawMatrix, double, double, double)->void',
        uiDrawMatrixSkew           => '(*@uiDrawMatrix, double, double, double, double)->void',
        uiDrawMatrixMultiply       => '(*@uiDrawMatrix, *@uiDrawMatrix)->void',
        uiDrawMatrixInvertible     => '(*@uiDrawMatrix)->int',
        uiDrawMatrixInvert         => '(*@uiDrawMatrix)->int',
        uiDrawMatrixTransformPoint => '(*@uiDrawMatrix, *double, *double)->void',
        uiDrawMatrixTransformSize  => '(*@uiDrawMatrix, *double, *double)->void',
        uiDrawTransform            => '(@uiDrawContext, *@uiDrawMatrix)->void',
        uiDrawClip                 => '(@uiDrawContext, @uiDrawPath)->void',
        uiDrawSave                 => '(@uiDrawContext)->void',
        uiDrawRestore              => '(@uiDrawContext)->void',

        # Font
        uiDrawLoadClosestFont => '(*{family: *char, size: double, weight: int, italic: int, stretch: int})->*void',
        uiDrawFreeFont        => '(*void)->void',
        uiDrawNewTextLayout   => '(*char, *void, double)->*void',
        uiDrawFreeTextLayout  => '(*void)->void',
        uiDrawText            => '(@uiDrawContext, *void, double, double)->void',

        # Area
        uiNewArea                   => '(*@uiAreaHandler)->@uiArea',
        uiNewScrollingArea          => '(*@uiAreaHandler, int, int)->@uiArea',
        uiAreaSetSize               => '(@uiArea, int, int)->void',
        uiAreaQueueRedrawAll        => '(@uiArea)->void',
        uiAreaScrollTo              => '(@uiArea, double, double, double, double)->void',
        uiAreaBeginUserWindowMove   => '(@uiArea)->void',
        uiAreaBeginUserWindowResize => '(@uiArea, int)->void',

        # FontButton
        uiNewFontButton       => '()->@uiFontButton',
        uiFontButtonFont      => '(@uiFontButton)->*void',
        uiFontButtonOnChanged => '(@uiFontButton, cb: * ((@uiFontButton, *void)->void), data: *void)->void',

        # ColorButton
        uiNewColorButton       => '()->@uiColorButton',
        uiColorButtonColor     => '(@uiColorButton, *double, *double, *double, *double)->void',
        uiColorButtonSetColor  => '(@uiColorButton, double, double, double, double)->void',
        uiColorButtonOnChanged => '(@uiColorButton, cb: * ((@uiColorButton, @SV)->void), data: @SV)->void',

        # Open/Save Dialogs
        uiOpenFile    => '(@uiWindow)->*char',
        uiSaveFile    => '(@uiWindow)->*char',
        uiMsgBox      => '(@uiWindow, *char, *char)->void',
        uiMsgBoxError => '(@uiWindow, *char, *char)->void',

        # Table Value
        uiNewTableValueString => '(*char)->@uiTableValue',
        uiTableValueString    => '(@uiTableValue)->*char',
        uiFreeTableValue      => '(@uiTableValue)->void',

        # Table Model
        uiNewTableModel               => '(*@uiTableModelHandler)->@uiTableModel',
        uiFreeTableModel              => '(@uiTableModel)->void',
        uiTableModelNotifyRowInserted => '(@uiTableModel, int)->void',
        uiTableModelNotifyRowChanged  => '(@uiTableModel, int)->void',
        uiTableModelNotifyRowDeleted  => '(@uiTableModel, int)->void',
        uiTableModelRowInserted       => '(@uiTableModel, int)->void',

        # Table View
        uiNewTable                   => '(*@uiTableParams)->@uiTable',
        uiTableAppendTextColumn      => '(@uiTable, *char, int, int)->void',
        uiTableAppendImageColumn     => '(@uiTable, *char, int)->void',
        uiTableAppendImageTextColumn => '(@uiTable, *char, int, int, int)->void',
        uiTableAppendCheckboxColumn  => '(@uiTable, *char, int, int)->void'
    ) {
        my $as = 'LibUI::' . $name;
        affix $lib, [ $name => $as ], $sig;

        #~ warn $lib;
        #~ warn $name;
        #~ warn $sig;
        push @EXPORT_OK, $name;
    }
    #
    our %EXPORT_TAGS = ( all => \@EXPORT_OK );
}
#
1;

=head1 NAME

LibUI - Perl bindings for the libui-ng cross-platform GUI library

=head1 VERSION

version 1.0.0

=head1 SYNOPSIS

A simple "Hello, World" application with a button that updates a label.

    use v5.36;
    use LibUI qw[:all];

    my $main_win;

    # Callback for when the window's close button is clicked.
    # Must call uiQuit() to exit the main event loop.
    sub on_closing ($window, $data) {
        say "Window closing. Bye!";
        uiQuit();
        return 1; # Return true to allow the window to close
    }

    # Callback for when the button is clicked.
    # The $label is passed as user data.
    sub on_button_clicked ($button, $label) {
        state $count = 0;
        $count++;
        uiLabelSetText($label, "You clicked me $count time(s)!");
    }

    # 1. Initialize the library
    my $init_error = uiInit({ Size => 0 }); # Pass a hashref for options
    if ($init_error) {
        die "Error initializing libui: $init_error";
    }

    # 2. Create the main window
    $main_win = uiNewWindow("Hello World!", 400, 100, 0);
    uiWindowSetMargined($main_win, 1);
    uiWindowOnClosing($main_win, \&on_closing, undef);

    # 3. Create widgets and layout
    my $vbox = uiNewVerticalBox();
    uiBoxSetPadded($vbox, 1);
    uiWindowSetChild($main_win, $vbox);

    my $label = uiNewLabel("Welcome to libui-ng from Perl!");
    uiBoxAppend($vbox, $label, 0); # 0 = not stretchy

    my $button = uiNewButton("Click Me");
    # Pass a reference to the label as the user data for the callback
    uiButtonOnClicked($button, \&on_button_clicked, $label);
    uiBoxAppend($vbox, $button, 0); # 0 = not stretchy

    # 4. Show the window and start the main event loop
    uiControlShow($main_win);
    uiMain();

    # 5. Clean up after the event loop ends
    uiUninit();
    say "Program finished cleanly.";

=head1 DESCRIPTION

This module provides Perl bindings for the L<libui-ng|https://github.com/libui-ng/libui-ng> C library, a lightweight and portable toolkit for creating native graphical user interfaces.

It uses L<Affix> to create a direct, thin wrapper around the C functions. This means the API closely mirrors the C API documented in the L<ui.h|https://github.com/libui-ng/libui-ng/blob/master/ui.h> header file. The goal is to be simple and direct rather than highly "Perlish" or object-oriented.

All widget objects are returned as opaque handles. You interact with them by passing the handle as the first argument to the various `uiWidget...` functions.

=head1 CORE CONCEPTS

=head2 Initialization and the Main Loop

A LibUI program has a clear lifecycle:

=over 4

=item 1. B<Initialize:> Call C<uiInit()> once at the beginning of your program.

=item 2. B<Create Widgets:> Create windows, buttons, boxes, and other controls.

=item 3. B<Run the Event Loop:> Call C<uiMain()>. This function blocks and hands control over to the GUI, waiting for user input. Your code will only execute in response to events (like button clicks) via callbacks.

=item 4. B<Quit and Clean Up:> A callback (e.g., from the window's close button) must call C<uiQuit()> to signal the event loop to exit. After C<uiMain()> returns, you should call C<uiUninit()> to free all resources.

=back

=head2 Widget Handles

Functions that create widgets (e.g., C<uiNewWindow>, C<uiNewButton>) return an opaque handle. This is an internal pointer to the C widget, wrapped in a way that Perl can use. You should store this handle in a variable and pass it as the first argument to any function that operates on that widget (e.g., C<uiWindowSetTitle($my_window, "New Title")>).

=head2 Parent-Child Hierarchy

Widgets are organized in a hierarchy. A C<uiWindow> can have one child widget. Container widgets, like C<uiBox>, can hold multiple children. You build your UI by creating widgets and adding them to containers using functions like C<uiWindowSetChild> and C<uiBoxAppend>.

=head2 Callbacks

LibUI is event-driven. You make your application interactive by registering B<callbacks>, which are simply Perl subroutine references.

When an event occurs (e.g., a button is clicked), LibUI invokes the corresponding callback you provided.

A typical callback registration looks like this:

    uiButtonOnClicked($button, \&my_callback, $some_data);

The callback subroutine will receive two arguments:

    sub my_callback ($widget_handle, $user_data) {
        # ... do something ...
    }

=over 4

=item C<$widget_handle>

The handle of the widget that triggered the event (e.g., the `$button` itself).

=item C<$user_data>

The third argument you passed to the registration function (`$some_data` in the example). This is extremely useful for giving your callback access to other variables in your program, such as other widgets it needs to modify.

=back

=head2 Structs as Hash References

Some C functions expect a pointer to a struct. This module maps C structs to Perl hash references. For example, to pass options to C<uiInit>, you provide a hash reference:

    uiInit( \{ Size => 0 } );

For drawing functions that require a C<uiDrawBrush>, you can create one on the fly:

    my $brush = {
        Type => uiDrawBrushTypeSolid,
        R    => 1.0, # Red
        G    => 0.0,
        B    => 0.0,
        A    => 1.0, # Alpha
    };
    uiDrawFill($draw_context, $path, $brush);

=head1 EXPORTS

This module exports nothing by default. All functions and constants are available for import. You can import everything using the C<:all> tag:

    use LibUI qw[:all];

=head1 FUNCTION REFERENCE

The function names are derived directly from the C library by removing the `ui` prefix and converting to Perl's standard `snake_case`. For example, C's `uiNewWindow` becomes Perl's `uiNewWindow`.

=head2 Main Lifecycle Functions

=over 4

=item C<my $err = uiInit( \{ Size => 0 } )>

Initializes the library. Must be called before any other LibUI function. Takes a hash reference for options (currently only `Size` is defined but is reserved for future use). Returns a string on error, `undef` on success.

=item C<uiUninit()>

De-initializes the library and frees all associated resources. Call this after C<uiMain> returns.

=item C<uiMain()>

Starts the main GUI event loop. This function blocks until C<uiQuit> is called.

=item C<uiQuit()>

Signals the main event loop to terminate. Typically called from a callback, like C<uiWindowOnClosing>.

=item C<uiFreeInitError($err)>

Frees the error string returned by a failed C<uiInit>.

=item C<uiQueueMain(\&sub, $data)>

Queues a subroutine to be executed on the main GUI thread.

=item C<uiOnShouldQuit(\&callback, $data)>

Registers a callback that is executed when the user requests to quit the application (e.g., via a standard "Quit" menu item on macOS). The callback should return `1` to allow the quit, and `0` to prevent it.

=back

=head2 Generic Control Functions

These functions operate on any widget handle (e.g., a window, button, box).

=over 4

=item C<uiControlDestroy($control)>

Destroys a widget.

=item C<uiControlHandle($control)>

Returns the raw OS-level handle for the widget (e.g., an `HWND` on Windows).

=item C<my $parent = uiControlParent($control)>

Returns the parent widget of the control.

=item C<uiControlSetParent($control, $parent)>

Sets the parent of the control.

=item C<my $is_toplevel = uiControlToplevel($control)>

Returns true if the control is a top-level widget (i.e., a window).

=item C<my $is_visible = uiControlVisible($control)>

Returns true if the control is currently visible.

=item C<uiControlShow($control)>

Shows the control.

=item C<uiControlHide($control)>

Hides the control.

=item C<my $is_enabled = uiControlEnabled($control)>

Returns true if the control is enabled.

=item C<uiControlEnable($control)>

Enables the control.

=item C<uiControlDisable($control)>

Disables the control.

=back

=head2 Window Functions

=over 4

=item C<my $win = uiNewWindow($title, $width, $height, $has_menubar)>

Creates a new window. C<$has_menubar> is a boolean (`0` or `1`).

=item C<my $title = uiWindowTitle($win)>

Gets the window's title. You are responsible for freeing this string with C<uiFreeText>.

=item C<uiWindowSetTitle($win, $title)>

Sets the window's title.

=item C<uiWindowContentSize($win, $width_ref, $height_ref)>

Gets the content size of the window. Takes references to two scalars and populates them.

=item C<uiWindowSetContentSize($win, $width, $height)>

Sets the content size of the window.

=item C<my $is_fullscreen = uiWindowFullscreen($win)>

Returns true if the window is in full-screen mode.

=item C<uiWindowSetFullscreen($win, $is_fullscreen)>

Sets the window's full-screen state.

=item C<my $is_borderless = uiWindowBorderless($win)>

Returns true if the window is borderless.

=item C<uiWindowSetBorderless($win, $is_borderless)>

Sets the window's borderless state.

=item C<my $child = uiWindowChild($win)>

Returns the single child widget of the window.

=item C<uiWindowSetChild($win, $child_control)>

Sets the single child widget for the window. This is typically a container like a C<uiBox>.

=item C<my $is_margined = uiWindowMargined($win)>

Returns true if the window has a margin around its content area.

=item C<uiWindowSetMargined($win, $is_margined)>

Sets the window's margin state.

=item C<uiWindowOnClosing(\&callback, $data)>

Registers a callback to be run when the user clicks the window's close button. The callback must return a true value to allow the window to close. This is where you should call C<uiQuit>.

=item C<uiWindowOnContentSizeChanged(\&callback, $data)>

Registers a callback for when the window's size changes.

=item C<uiWindowOnPositionChanged(\&callback, $data)>

Registers a callback for when the window's position changes.

=back

=head2 Button Functions

=over 4

=item C<my $btn = uiNewButton($text)>

Creates a new button.

=item C<my $text = uiButtonText($btn)>

Gets the button's text. Free with C<uiFreeText>.

=item C<uiButtonSetText($btn, $text)>

Sets the button's text.

=item C<uiButtonOnClicked($btn, \&callback, $data)>

Registers a callback for when the button is clicked.

=back

=head2 Box Functions (Layout Containers)

=over 4

=item C<my $hbox = uiNewHorizontalBox()>

Creates a new horizontal box container. Children are arranged from left to right.

=item C<my $vbox = uiNewVerticalBox()>

Creates a new vertical box container. Children are arranged from top to bottom.

=item C<uiBoxAppend($box, $child, $stretchy)>

Appends a widget to the box. If C<$stretchy> is true (`1`), the widget will expand to fill available space.

=item C<uiBoxDelete($box, $index)>

Removes the widget at the given index from the box.

=item C<my $is_padded = uiBoxPadded($box)>

Returns true if there is padding between the children of the box.

=item C<uiBoxSetPadded($box, $is_padded)>

Sets the padding state for the box.

=back

=head2 Checkbox Functions

=over 4

=item C<my $cb = uiNewCheckbox($text)>

Creates a new checkbox.

=item C<my $text = uiCheckboxText($cb)>

Gets the checkbox's label text. Free with C<uiFreeText>.

=item C<uiCheckboxSetText($cb, $text)>

Sets the checkbox's label text.

=item C<my $is_checked = uiCheckboxChecked($cb)>

Returns true if the checkbox is checked.

=item C<uiCheckboxSetChecked($cb, $is_checked)>

Sets the checked state of the checkbox.

=item C<uiCheckboxOnToggled($cb, \&callback, $data)>

Registers a callback for when the checkbox's state changes.

=back

=head2 And many more...

This module provides wrappers for the entire libui-ng API. The function names and signatures are designed to be predictable based on the C header file. Please refer to L<ui.h|https://github.com/libui-ng/libui-ng/blob/master/ui.h> for a complete list of functions, and use the following mapping rules:

=over 4

=item *   The C function `uiWidgetAction` becomes Perl's `uiWidgetAction`.
=item *   Widget handles (e.g., `uiWindow*`) are the first argument.
=item *   Callbacks are passed as `\&subroutine` references.
=item *   Struct pointers (e.g., `uiInitOptions*`) are passed as hash references.
=item *   Pointer arguments for "out" parameters (e.g., `int*`) are passed as scalar references (`\my $var`).

=back

=head1 CONSTANTS

The following constants are available for export.

=head2 Alignment (for Grids)

C<uiAlignFill>, C<uiAlignStart>, C<uiAlignCenter>, C<uiAlignEnd>

=head2 Alignment (for Grids)

C<uiAtLeading>, C<uiAtTop>, C<uiAtTrailing>, C<uiAtBottom>

=head2 Drawing - Brush Types

C<uiDrawBrushTypeSolid>, C<uiDrawBrushTypeLinearGradient>, C<uiDrawBrushTypeRadialGradient>, C<uiDrawBrushTypeImage>

=head2 Drawing - Line Caps

C<uiDrawLineCapFlat>, C<uiDrawLineCapRound>, C<uiDrawLineCapSquare>

=head2 Drawing - Line Joins

C<uiDrawLineJoinMiter>, C<uiDrawLineJoinRound>, C<uiDrawLineJoinBevel>

=head2 Drawing - Fill Modes

C<uiDrawFillModeWinding>, C<uiDrawFillModeAlternate>

=head2 Keyboard Modifiers

C<uiModifierCtrl>, C<uiModifierAlt>, C<uiModifierShift>, C<uiModifierSuper>

=head2 Extended Keys

C<uiExtKeyEscape>, C<uiExtKeyInsert>, C<uiExtKeyDelete>, C<uiExtKeyHome>, C<uiExtKeyEnd>, C<uiExtKeyPageUp>, C<uiExtKeyPageDown>, C<uiExtKeyUp>, C<uiExtKeyDown>, C<uiExtKeyLeft>, C<uiExtKeyRight>, C<uiExtKeyF1> through C<uiExtKeyF12>, C<uiExtKeyN0> through C<uiExtKeyN9>, C<uiExtKeyNDot>, C<uiExtKeyNEnter>, C<uiExtKeyNAdd>, C<uiExtKeyNSubtract>, C<uiExtKeyNMultiply>, C<uiExtKeyNDivide>

=head2 Window Resize Edges

C<uiWindowResizeEdgeLeft>, C<uiWindowResizeEdgeTop>, C<uiWindowResizeEdgeRight>, C<uiWindowResizeEdgeBottom>, C<uiWindowResizeEdgeTopLeft>, C<uiWindowResizeEdgeTopRight>, C<uiWindowResizeEdgeBottomLeft>, C<uiWindowResizeEdgeBottomRight>

=head1 SEE ALSO

=over 4

=item *   L<Affix> - The FFI layer used to build these bindings.

=item *   L<libui-ng GitHub Repository|https://github.com/libui-ng/libui-ng> - The homepage of the underlying C library.

=item *   L<ui.h|https://github.com/libui-ng/libui-ng/blob/master/ui.h> - The C header file, which serves as the primary API documentation.

=back

=head1 AUTHOR

Your Name <youremail@example.com>

=head1 COPYRIGHT AND LICENSE

This software is copyright (c) 2025 by Your Name.

This is free software; you can redistribute it and/or modify it under
the same terms as the Perl 5 programming language system itself.

=cut
