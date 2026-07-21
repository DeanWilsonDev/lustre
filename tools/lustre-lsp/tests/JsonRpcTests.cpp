#include "cimmerian/test.hpp"

#include "JsonRpc.h"

#include <cstdio>

DESCRIBE("JsonRpc", {
    IT("round-trips a message through Content-Length framing", {
        std::FILE* Temp = std::tmpfile();
        REQUIRE_TRUE(Temp != nullptr);

        Amanuensis::Value Message = Amanuensis::Value::MakeObject();
        Message.Insert("jsonrpc", Amanuensis::Value("2.0"));
        Message.Insert("id", Amanuensis::Value(42));
        Message.Insert("method", Amanuensis::Value("test/echo"));
        LustreLsp::JsonRpc::WriteMessage(Temp, Message);

        std::rewind(Temp);
        const auto Read = LustreLsp::JsonRpc::ReadMessage(Temp);
        REQUIRE_TRUE(Read.has_value());
        ASSERT_TRUE(Read->Get("method").AsString() == "test/echo");
        ASSERT_TRUE(Read->Get("id").AsInteger() == 42);
        std::fclose(Temp);
    });

    IT("reads two consecutive messages off the same stream", {
        std::FILE* Temp = std::tmpfile();
        REQUIRE_TRUE(Temp != nullptr);

        Amanuensis::Value First = Amanuensis::Value::MakeObject();
        First.Insert("method", Amanuensis::Value("first"));
        Amanuensis::Value Second = Amanuensis::Value::MakeObject();
        Second.Insert("method", Amanuensis::Value("second"));
        LustreLsp::JsonRpc::WriteMessage(Temp, First);
        LustreLsp::JsonRpc::WriteMessage(Temp, Second);

        std::rewind(Temp);
        const auto Read1 = LustreLsp::JsonRpc::ReadMessage(Temp);
        const auto Read2 = LustreLsp::JsonRpc::ReadMessage(Temp);
        REQUIRE_TRUE(Read1.has_value());
        REQUIRE_TRUE(Read2.has_value());
        ASSERT_TRUE(Read1->Get("method").AsString() == "first");
        ASSERT_TRUE(Read2->Get("method").AsString() == "second");
        std::fclose(Temp);
    });

    IT("ReadMessage returns nullopt at EOF with nothing written", {
        std::FILE* Temp = std::tmpfile();
        REQUIRE_TRUE(Temp != nullptr);
        std::rewind(Temp);
        ASSERT_FALSE(LustreLsp::JsonRpc::ReadMessage(Temp).has_value());
        std::fclose(Temp);
    });
});
