#include "harness.hpp"

#include "core/json.hpp"

using namespace eidolon;

TEST(json_parse_object) {
  JsonValue v;
  CHECK(jsonParse(R"({"a":1,"b":"two","c":true,"d":null,"e":[1,2.5,"x"]})", v));
  CHECK_EQ(v.num("a"), 1.0);
  CHECK_EQ(v.str("b"), std::string("two"));
  CHECK(v.boolean("c"));
  CHECK_EQ(v.type(), JsonValue::Type::Object);
  const JsonValue* e = v.find("e");
  CHECK(e && e->type() == JsonValue::Type::Array);
  CHECK_EQ(e->asArray().size(), 3u);
  CHECK_EQ(e->asArray()[1].asNumber(), 2.5);
}

TEST(json_parse_escapes) {
  JsonValue v;
  CHECK(jsonParse(R"({"s":"a\"b\\c\nd\u0041"})", v));
  const std::string& s = v.str("s");
  CHECK_EQ(s, std::string("a\"b\\c\ndA"));
}

TEST(json_parse_rejects_garbage) {
  JsonValue v;
  CHECK(!jsonParse("", v));
  CHECK(!jsonParse("{", v));
  CHECK(!jsonParse("[1,2", v));
  CHECK(!jsonParse("hello", v));
  CHECK(!jsonParse("{\"a\":}", v));
}

TEST(json_dump_roundtrip) {
  JsonValue v = JsonValue::makeObject();
  v.setNumber("n", 3.5);
  v.setString("s", "hi \"there\"\n");
  v.setBool("b", true);
  JsonValue arr = JsonValue::makeArray();
  arr.push(JsonValue::makeNumber(1));
  arr.push(JsonValue::makeString("x"));
  v.set("arr", arr);
  JsonValue parsed;
  CHECK(jsonParse(v.dump(), parsed));
  CHECK_EQ(parsed.num("n"), 3.5);
  CHECK_EQ(parsed.str("s"), std::string("hi \"there\"\n"));
  CHECK(parsed.boolean("b"));
  CHECK_EQ(parsed.find("arr")->asArray().size(), 2u);
}

TEST(json_nested_objects) {
  JsonValue v;
  CHECK(jsonParse(R"({"outer":{"inner":[{"a":1},{"b":2}]}})", v));
  const JsonValue* outer = v.find("outer");
  CHECK(outer != nullptr);
  const JsonValue* inner = outer->find("inner");
  CHECK(inner && inner->type() == JsonValue::Type::Array);
  CHECK_EQ(inner->asArray()[1].num("b"), 2.0);
}