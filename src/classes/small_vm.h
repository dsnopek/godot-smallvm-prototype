#pragma once

#include "godot_cpp/classes/ref_counted.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/variant/variant.hpp"

using namespace godot;

class SmallVM : public RefCounted {
	GDCLASS(SmallVM, RefCounted)

protected:
	static void _bind_methods();

public:
	SmallVM() = default;
	~SmallVM() override = default;

	void print_type(const Variant &p_variant) const;
	void execute_bytecode(const PackedByteArray &p_bytecode, Node2D *p_current_object);
};
