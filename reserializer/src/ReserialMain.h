#pragma once

enum ResourceType : unsigned;
class BinaryWriter;
class EntNode;

namespace Reserializer
{
	// Returns the number of warnings thrown when reserializing the file
	// A return value of 0 means no warnings
	int Serialize(const EntNode& root, BinaryWriter& writer, ResourceType restype);

	// Returns the number of warnings thrown when reserializing the file
	// A return value of 0 means no warnings
	int Serialize(const char* data, size_t length, BinaryWriter& writer, ResourceType restype);

	// Returns the number of warnings thrown when reserializing the file
	// A return value of 0 means no warnings
	int Serialize(const char* filepath, BinaryWriter& writer, ResourceType restype);

	// Determines whether the data stream is serialized
	bool IsSerialized(const char* data, size_t length, ResourceType restype);
}