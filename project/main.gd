extends Node2D

@onready var sprite: Sprite2D = %Sprite2D

var smallvm := SmallVM.new()
var bytecode: PackedByteArray


func _ready() -> void:
	bytecode = FileAccess.get_file_as_bytes("res://my_script.ubcode")


func _physics_process(_delta: float) -> void:
	if sprite and bytecode.size() > 0:
		smallvm.execute_bytecode(bytecode, sprite)
