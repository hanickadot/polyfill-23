function(move_only_function)
	set(result "")
	
	function(generate CV REF NOEXCEPT)
		set(TEMP_FILE "move_only_function.tmp")
		configure_file("${SOURCE_DIRECTORY}/templates/move_only_function.in" ${TEMP_FILE})
		file(READ ${TEMP_FILE} content)
		file(REMOVE ${TEMP_FILE})
		string(APPEND result "${content}")
		set(result "${result}" PARENT_SCOPE)
	endfunction()

	generate("" "" "false")
	generate("" "" "true")
	generate("const" "" "false")
	generate("const" "" "true")

	generate("" "&" "false")
	generate("" "&" "true")
	generate("const" "&" "false")
	generate("const" "&" "true")

	generate("" "&&" "false")
	generate("" "&&" "true")
	generate("const" "&&" "false")
	generate("const" "&&" "true")

	configure_file("${SOURCE_DIRECTORY}/move_only_function.hpp.in" "${SOURCE_DIRECTORY}/move_only_function.hpp")
endfunction()

move_only_function()