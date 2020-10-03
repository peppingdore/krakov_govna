#include "Editor.h"

#include "Input.h"

void Editor::init()
{

}

void Editor::do_frame()
{
	if (input.is_key_down(Key::F1))
	{
		editor.is_open = !editor.is_open;
	}

	if (!editor.is_open) return;


	scoped_set_and_revert(ui.parameters.text_font_face_size, 8);

	if (ui.button(Rect::make_from_center_and_size(renderer.width / 2, renderer.height / 2, 200, 40), U"Editor button", rgba(50, 50, 60, 255), ui_id(0)))
	{

	}
}