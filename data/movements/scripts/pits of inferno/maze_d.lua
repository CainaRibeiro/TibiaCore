function onStepIn(creature, item, position, fromPosition)
	local player = creature:getPlayer()
	if not player then
		return false
	end

	player:teleportTo(Position(32840,32317, 9))
	return true
end

function onAddItem(item, tileitem, position)
	doRelocate(item:getPosition(),{x = 32840, y = 32317, z = 9})
end