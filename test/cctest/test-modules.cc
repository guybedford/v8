// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/flags.h"
#include "src/ostreams.h"

#include "test/cctest/cctest.h"

namespace {

using v8::Array;
using v8::Context;
using v8::DynamicModule;
using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Module;
using v8::ScriptCompiler;
using v8::ScriptOrigin;
using v8::String;
using v8::Value;

ScriptOrigin ModuleOrigin(Local<v8::Value> resource_name, Isolate* isolate) {
  ScriptOrigin origin(resource_name, Local<v8::Integer>(), Local<v8::Integer>(),
                      Local<v8::Boolean>(), Local<v8::Integer>(),
                      Local<v8::Value>(), Local<v8::Boolean>(),
                      Local<v8::Boolean>(), True(isolate));
  return origin;
}

static Local<Module> dep1;
static Local<Module> dep2;
MaybeLocal<Module> ResolveCallback(Local<Context> context,
                                   Local<String> specifier,
                                   Local<Module> referrer) {
  Isolate* isolate = CcTest::isolate();
  if (specifier->StrictEquals(v8_str("./dep1.js"))) {
    return dep1;
  } else if (specifier->StrictEquals(v8_str("./dep2.js"))) {
    return dep2;
  } else {
    isolate->ThrowException(v8_str("boom"));
    return MaybeLocal<Module>();
  }
}

TEST(ModuleInstantiationFailures1) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  Local<Module> module;
  {
    Local<String> source_text = v8_str(
        "import './foo.js';\n"
        "export {} from './bar.js';");
    ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    module = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    CHECK_EQ(2, module->GetModuleRequestsLength());
    CHECK(v8_str("./foo.js")->StrictEquals(module->GetModuleRequest(0)));
    v8::Location loc = module->GetModuleRequestLocation(0);
    CHECK_EQ(0, loc.GetLineNumber());
    CHECK_EQ(7, loc.GetColumnNumber());
    CHECK(v8_str("./bar.js")->StrictEquals(module->GetModuleRequest(1)));
    loc = module->GetModuleRequestLocation(1);
    CHECK_EQ(1, loc.GetLineNumber());
    CHECK_EQ(15, loc.GetColumnNumber());
  }

  // Instantiation should fail.
  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(module->InstantiateModule(env.local(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
  }

  // Start over again...
  {
    Local<String> source_text = v8_str(
        "import './dep1.js';\n"
        "export {} from './bar.js';");
    ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    module = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  // dep1.js
  {
    Local<String> source_text = v8_str("");
    ScriptOrigin origin = ModuleOrigin(v8_str("dep1.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep1 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  // Instantiation should fail because a sub-module fails to resolve.
  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(module->InstantiateModule(env.local(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
  }

  CHECK(!try_catch.HasCaught());
}

TEST(ModuleInstantiationFailures2) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  // root1.js
  Local<Module> root;
  {
    Local<String> source_text =
        v8_str("import './dep1.js'; import './dep2.js'");
    ScriptOrigin origin = ModuleOrigin(v8_str("root1.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    root = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  // dep1.js
  {
    Local<String> source_text = v8_str("export let x = 42");
    ScriptOrigin origin = ModuleOrigin(v8_str("dep1.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep1 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  // dep2.js
  {
    Local<String> source_text = v8_str("import {foo} from './dep3.js'");
    ScriptOrigin origin = ModuleOrigin(v8_str("dep2.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep2 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(root->InstantiateModule(env.local(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kUninstantiated, root->GetStatus());
    CHECK_EQ(Module::kUninstantiated, dep1->GetStatus());
    CHECK_EQ(Module::kUninstantiated, dep2->GetStatus());
  }

  // Change dep2.js
  {
    Local<String> source_text = v8_str("import {foo} from './dep2.js'");
    ScriptOrigin origin = ModuleOrigin(v8_str("dep2.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep2 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(root->InstantiateModule(env.local(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(!inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kUninstantiated, root->GetStatus());
    CHECK_EQ(Module::kInstantiated, dep1->GetStatus());
    CHECK_EQ(Module::kUninstantiated, dep2->GetStatus());
  }

  // Change dep2.js again
  {
    Local<String> source_text = v8_str("import {foo} from './dep3.js'");
    ScriptOrigin origin = ModuleOrigin(v8_str("dep2.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep2 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(root->InstantiateModule(env.local(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kUninstantiated, root->GetStatus());
    CHECK_EQ(Module::kInstantiated, dep1->GetStatus());
    CHECK_EQ(Module::kUninstantiated, dep2->GetStatus());
  }
}

static MaybeLocal<Module> CompileSpecifierAsModuleResolveCallback(
    Local<Context> context, Local<String> specifier, Local<Module> referrer) {
  ScriptOrigin origin = ModuleOrigin(v8_str("module.js"), CcTest::isolate());
  ScriptCompiler::Source source(specifier, origin);
  return ScriptCompiler::CompileModule(CcTest::isolate(), &source)
      .ToLocalChecked();
}

TEST(ModuleEvaluation) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  Local<String> source_text = v8_str(
      "import 'Object.expando = 5';"
      "import 'Object.expando *= 2';");
  ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
  ScriptCompiler::Source source(source_text, origin);
  Local<Module> module =
      ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  CHECK_EQ(Module::kUninstantiated, module->GetStatus());
  CHECK(module
            ->InstantiateModule(env.local(),
                                CompileSpecifierAsModuleResolveCallback)
            .FromJust());
  CHECK_EQ(Module::kInstantiated, module->GetStatus());
  CHECK(!module->Evaluate(env.local()).IsEmpty());
  CHECK_EQ(Module::kEvaluated, module->GetStatus());
  ExpectInt32("Object.expando", 10);

  CHECK(!try_catch.HasCaught());
}

TEST(ModuleEvaluationError) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  Local<String> source_text =
      v8_str("Object.x = (Object.x || 0) + 1; throw 'boom';");
  ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
  ScriptCompiler::Source source(source_text, origin);
  Local<Module> module =
      ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  CHECK_EQ(Module::kUninstantiated, module->GetStatus());
  CHECK(module
            ->InstantiateModule(env.local(),
                                CompileSpecifierAsModuleResolveCallback)
            .FromJust());
  CHECK_EQ(Module::kInstantiated, module->GetStatus());

  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(module->Evaluate(env.local()).IsEmpty());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kErrored, module->GetStatus());
    Local<Value> exception = module->GetException();
    CHECK(exception->StrictEquals(v8_str("boom")));
    ExpectInt32("Object.x", 1);
  }

  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(module->Evaluate(env.local()).IsEmpty());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kErrored, module->GetStatus());
    Local<Value> exception = module->GetException();
    CHECK(exception->StrictEquals(v8_str("boom")));
    ExpectInt32("Object.x", 1);
  }

  CHECK(!try_catch.HasCaught());
}

TEST(ModuleEvaluationCompletion1) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  const char* sources[] = {
      "",
      "var a = 1",
      "import '42'",
      "export * from '42'",
      "export {} from '42'",
      "export {}",
      "var a = 1; export {a}",
      "export function foo() {}",
      "export class C extends null {}",
      "export let a = 1",
      "export default 1",
      "export default function foo() {}",
      "export default function () {}",
      "export default (function () {})",
      "export default class C extends null {}",
      "export default (class C extends null {})",
      "for (var i = 0; i < 5; ++i) {}",
  };

  for (auto src : sources) {
    Local<String> source_text = v8_str(src);
    ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> module =
        ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    CHECK(module
              ->InstantiateModule(env.local(),
                                  CompileSpecifierAsModuleResolveCallback)
              .FromJust());
    CHECK_EQ(Module::kInstantiated, module->GetStatus());
    CHECK(module->Evaluate(env.local()).ToLocalChecked()->IsUndefined());
    CHECK_EQ(Module::kEvaluated, module->GetStatus());
    CHECK(module->Evaluate(env.local()).ToLocalChecked()->IsUndefined());
    CHECK_EQ(Module::kEvaluated, module->GetStatus());
  }

  CHECK(!try_catch.HasCaught());
}

TEST(ModuleEvaluationCompletion2) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  const char* sources[] = {
      "'gaga'; ",
      "'gaga'; var a = 1",
      "'gaga'; import '42'",
      "'gaga'; export * from '42'",
      "'gaga'; export {} from '42'",
      "'gaga'; export {}",
      "'gaga'; var a = 1; export {a}",
      "'gaga'; export function foo() {}",
      "'gaga'; export class C extends null {}",
      "'gaga'; export let a = 1",
      "'gaga'; export default 1",
      "'gaga'; export default function foo() {}",
      "'gaga'; export default function () {}",
      "'gaga'; export default (function () {})",
      "'gaga'; export default class C extends null {}",
      "'gaga'; export default (class C extends null {})",
  };

  for (auto src : sources) {
    Local<String> source_text = v8_str(src);
    ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> module =
        ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    CHECK(module
              ->InstantiateModule(env.local(),
                                  CompileSpecifierAsModuleResolveCallback)
              .FromJust());
    CHECK_EQ(Module::kInstantiated, module->GetStatus());
    CHECK(module->Evaluate(env.local())
              .ToLocalChecked()
              ->StrictEquals(v8_str("gaga")));
    CHECK_EQ(Module::kEvaluated, module->GetStatus());
    CHECK(module->Evaluate(env.local()).ToLocalChecked()->IsUndefined());
    CHECK_EQ(Module::kEvaluated, module->GetStatus());
  }

  CHECK(!try_catch.HasCaught());
}

TEST(ModuleNamespace) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  Local<v8::Object> ReferenceError =
      CompileRun("ReferenceError")->ToObject(env.local()).ToLocalChecked();

  Local<String> source_text = v8_str(
      "import {a, b} from 'export var a = 1; export let b = 2';"
      "export function geta() {return a};"
      "export function getb() {return b};"
      "export let radio = 3;"
      "export var gaga = 4;");
  ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
  ScriptCompiler::Source source(source_text, origin);
  Local<Module> module =
      ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  CHECK_EQ(Module::kUninstantiated, module->GetStatus());

  CHECK(module
            ->InstantiateModule(env.local(),
                                CompileSpecifierAsModuleResolveCallback)
            .FromJust());
  CHECK_EQ(Module::kInstantiated, module->GetStatus());

  Local<Value> ns = module->GetModuleNamespace();
  CHECK_EQ(Module::kInstantiated, module->GetStatus());
  Local<v8::Object> nsobj = ns->ToObject(env.local()).ToLocalChecked();

  // a, b
  CHECK(nsobj->Get(env.local(), v8_str("a")).ToLocalChecked()->IsUndefined());
  CHECK(nsobj->Get(env.local(), v8_str("b")).ToLocalChecked()->IsUndefined());

  // geta
  {
    auto geta = nsobj->Get(env.local(), v8_str("geta")).ToLocalChecked();
    auto a = geta.As<v8::Function>()
                 ->Call(env.local(), geta, 0, nullptr)
                 .ToLocalChecked();
    CHECK(a->IsUndefined());
  }

  // getb
  {
    v8::TryCatch inner_try_catch(isolate);
    auto getb = nsobj->Get(env.local(), v8_str("getb")).ToLocalChecked();
    CHECK(
        getb.As<v8::Function>()->Call(env.local(), getb, 0, nullptr).IsEmpty());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()
              ->InstanceOf(env.local(), ReferenceError)
              .FromJust());
  }

  // radio
  {
    v8::TryCatch inner_try_catch(isolate);
    // https://bugs.chromium.org/p/v8/issues/detail?id=7235
    // CHECK(nsobj->Get(env.local(), v8_str("radio")).IsEmpty());
    CHECK(nsobj->Get(env.local(), v8_str("radio"))
              .ToLocalChecked()
              ->IsUndefined());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()
              ->InstanceOf(env.local(), ReferenceError)
              .FromJust());
  }

  // gaga
  {
    auto gaga = nsobj->Get(env.local(), v8_str("gaga")).ToLocalChecked();
    CHECK(gaga->IsUndefined());
  }

  CHECK(!try_catch.HasCaught());
  CHECK_EQ(Module::kInstantiated, module->GetStatus());

  module->Evaluate(env.local()).ToLocalChecked();
  CHECK_EQ(Module::kEvaluated, module->GetStatus());

  // geta
  {
    auto geta = nsobj->Get(env.local(), v8_str("geta")).ToLocalChecked();
    auto a = geta.As<v8::Function>()
                 ->Call(env.local(), geta, 0, nullptr)
                 .ToLocalChecked();
    CHECK_EQ(1, a->Int32Value(env.local()).FromJust());
  }

  // getb
  {
    auto getb = nsobj->Get(env.local(), v8_str("getb")).ToLocalChecked();
    auto b = getb.As<v8::Function>()
                 ->Call(env.local(), getb, 0, nullptr)
                 .ToLocalChecked();
    CHECK_EQ(2, b->Int32Value(env.local()).FromJust());
  }

  // radio
  {
    auto radio = nsobj->Get(env.local(), v8_str("radio")).ToLocalChecked();
    CHECK_EQ(3, radio->Int32Value(env.local()).FromJust());
  }

  // gaga
  {
    auto gaga = nsobj->Get(env.local(), v8_str("gaga")).ToLocalChecked();
    CHECK_EQ(4, gaga->Int32Value(env.local()).FromJust());
  }

  CHECK(!try_catch.HasCaught());
}

static Local<DynamicModule> dynamic;

void HostExecuteDynamicModuleCallback(Local<Context> context,
                                      Local<DynamicModule> module) {
  Isolate* isolate = context->GetIsolate();
  auto val = v8::Number::New(isolate, 10);
  module->SetExport(isolate, v8_str("test"), val);
}

MaybeLocal<Module> ResolveCallbackDynamicModule(Local<Context> context,
                                                Local<String> specifier,
                                                Local<Module> referrer) {
  Isolate* isolate = CcTest::isolate();
  if (specifier->StrictEquals(v8_str("dynamic"))) {
    return dynamic;
  } else if (specifier->StrictEquals(v8_str("./dep1.js"))) {
    return dep1;
  } else if (specifier->StrictEquals(v8_str("./dep2.js"))) {
    return dep2;
  } else {
    isolate->ThrowException(v8_str("boom"));
    return MaybeLocal<Module>();
  }
}

TEST(DynamicModule) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  dynamic = ScriptCompiler::CreateDynamicModule(isolate).ToLocalChecked();

  isolate->SetHostExecuteDynamicModuleCallback(
      HostExecuteDynamicModuleCallback);

  Local<String> source_text = v8_str("export { test as p } from 'dynamic'");
  ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
  ScriptCompiler::Source source(source_text, origin);
  Local<Module> module =
      ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  CHECK_EQ(module->GetStatus(), Module::kUninstantiated);
  CHECK(module->InstantiateModule(env.local(), ResolveCallbackDynamicModule)
            .FromJust());

  CHECK_EQ(Module::kInstantiated, module->GetStatus());
  CHECK_EQ(Module::kInstantiated, dynamic->GetStatus());

  module->Evaluate(env.local()).ToLocalChecked();

  CHECK_EQ(Module::kEvaluated, module->GetStatus());
  CHECK_EQ(Module::kEvaluated, dynamic->GetStatus());

  Local<Value> ns = module->GetModuleNamespace();
  Local<v8::Object> nsobj = ns->ToObject(env.local()).ToLocalChecked();

  // export was set by dynamic execute hook
  {
    auto testVal = nsobj->Get(env.local(), v8_str("p")).ToLocalChecked();
    CHECK_EQ(10, testVal->Int32Value(env.local()).FromJust());
  }

  // export can be mutated
  {
    auto val = v8::Number::New(isolate, 5);
    dynamic->SetExport(isolate, v8_str("test"), val);

    auto testVal = nsobj->Get(env.local(), v8_str("p")).ToLocalChecked();
    CHECK_EQ(5, testVal->Int32Value(env.local()).FromJust());
  }

  CHECK(!try_catch.HasCaught());
}

TEST(DynamicModuleNamespaces) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  dynamic = ScriptCompiler::CreateDynamicModule(isolate).ToLocalChecked();

  isolate->SetHostExecuteDynamicModuleCallback(
      HostExecuteDynamicModuleCallback);

  {
    Local<String> source_text = v8_str("export * from 'dynamic'");
    ScriptOrigin origin = ModuleOrigin(v8_str("dep1.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep1 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  Local<String> source_text =
      v8_str("import * as X from './dep1.js'; export { X }");
  ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
  ScriptCompiler::Source source(source_text, origin);
  Local<Module> module =
      ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  CHECK_EQ(module->GetStatus(), Module::kUninstantiated);
  CHECK(module->InstantiateModule(env.local(), ResolveCallbackDynamicModule)
            .FromJust());

  CHECK_EQ(Module::kInstantiated, module->GetStatus());
  CHECK_EQ(Module::kInstantiated, dep1->GetStatus());
  CHECK_EQ(Module::kInstantiated, dynamic->GetStatus());

  module->Evaluate(env.local()).ToLocalChecked();

  CHECK_EQ(Module::kEvaluated, module->GetStatus());
  CHECK_EQ(Module::kEvaluated, dep1->GetStatus());
  CHECK_EQ(Module::kEvaluated, dynamic->GetStatus());

  {
    Local<Value> ns = dynamic->GetModuleNamespace();
    Local<v8::Object> nsobj = ns->ToObject(env.local()).ToLocalChecked();

    auto testVal = nsobj->Get(env.local(), v8_str("test")).ToLocalChecked();
    CHECK_EQ(10, testVal->Int32Value(env.local()).FromJust());
  }

  {
    Local<Value> ns = dep1->GetModuleNamespace();
    Local<v8::Object> nsobj = ns->ToObject(env.local()).ToLocalChecked();

    auto testVal = nsobj->Get(env.local(), v8_str("test")).ToLocalChecked();
    CHECK_EQ(10, testVal->Int32Value(env.local()).FromJust());
  }

  Local<Value> ns = module->GetModuleNamespace();
  Local<v8::Object> nsobj = ns->ToObject(env.local()).ToLocalChecked();

  Local<Value> dyn_ns = nsobj->Get(env.local(), v8_str("X")).ToLocalChecked();
  Local<v8::Object> dyn_nsobj = dyn_ns->ToObject(env.local()).ToLocalChecked();

  auto testVal = dyn_nsobj->Get(env.local(), v8_str("test")).ToLocalChecked();
  CHECK_EQ(10, testVal->Int32Value(env.local()).FromJust());

  CHECK(!try_catch.HasCaught());
}

TEST(DynamicStarExportsFail) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  dynamic = ScriptCompiler::CreateDynamicModule(isolate).ToLocalChecked();

  isolate->SetHostExecuteDynamicModuleCallback(
      HostExecuteDynamicModuleCallback);

  Local<String> source_text = v8_str("export * from 'dynamic'");
  ScriptOrigin origin = ModuleOrigin(v8_str("dep1.js"), CcTest::isolate());
  ScriptCompiler::Source source(source_text, origin);
  dep1 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();

  CHECK_EQ(dep1->GetStatus(), Module::kUninstantiated);

  CHECK(dep1->InstantiateModule(env.local(), ResolveCallbackDynamicModule)
            .IsNothing());

  CHECK_EQ(dep1->GetStatus(), Module::kErrored);
  CHECK_EQ(dynamic->GetStatus(), Module::kUninstantiated);

  Local<v8::Object> SyntaxError =
      CompileRun("SyntaxError")->ToObject(env.local()).ToLocalChecked();

  CHECK(try_catch.HasCaught());
  CHECK(try_catch.Exception()->InstanceOf(env.local(), SyntaxError).FromJust());
}

TEST(DynamicUnfinishedModuleNamespaces) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  dynamic = ScriptCompiler::CreateDynamicModule(isolate).ToLocalChecked();

  isolate->SetHostExecuteDynamicModuleCallback(
      HostExecuteDynamicModuleCallback);

  {
    Local<String> source_text = v8_str(
        "import './dep2.js';"
        "import * as X from 'dynamic';"
        "export function getNS () { return X; }");
    ScriptOrigin origin = ModuleOrigin(v8_str("dep1.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep1 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  {
    Local<String> source_text = v8_str(
        "import { getNS } from './dep1.js';"
        "const X = getNS();"
        "import { test as _test } from './dep2.js';"
        "export var exists = !!X;"
        "export var test = X.test;"
        "export var toStringTag = X[Symbol.toStringTag];"
        "export var isExtensible = Object.isExtensible(X);"
        "export var proto = X.__proto__;"
        "export var keys = Object.keys(X).join(',');"
        "export var ownKeys = "
        "  Reflect.ownKeys(X).map(x => x.toString()).join(',');"
        "export var ns = X;"
        "export var definedProperty = true;"
        "try { Object.defineProperty(X, 'test', { value () {} }); }"
        "catch { definedProperty = false; }"
        "export var preventedExtensions = true;"
        "try { Object.preventExtensions(X); }"
        "catch { preventedExtensions = false; }");
    ScriptOrigin origin = ModuleOrigin(v8_str("dep2.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep2 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  CHECK_EQ(dep1->GetStatus(), Module::kUninstantiated);
  CHECK(dep1->InstantiateModule(env.local(), ResolveCallbackDynamicModule)
            .FromJust());

  CHECK_EQ(Module::kInstantiated, dep1->GetStatus());
  CHECK_EQ(Module::kInstantiated, dep2->GetStatus());
  CHECK_EQ(Module::kInstantiated, dynamic->GetStatus());

  Local<Value> ns = dynamic->GetModuleNamespace();
  Local<v8::Object> nsobj = ns->ToObject(env.local()).ToLocalChecked();

  auto testVal = nsobj->Get(env.local(), v8_str("test")).ToLocalChecked();
  CHECK(testVal->IsUndefined());

  dep1->Evaluate(env.local()).ToLocalChecked();

  CHECK_EQ(Module::kEvaluated, dep1->GetStatus());
  CHECK_EQ(Module::kEvaluated, dep2->GetStatus());
  CHECK_EQ(Module::kEvaluated, dynamic->GetStatus());

  testVal = nsobj->Get(env.local(), v8_str("test")).ToLocalChecked();
  CHECK_EQ(10, testVal->Int32Value(env.local()).FromJust());

  {
    Local<Value> ns = dep2->GetModuleNamespace();
    Local<v8::Object> nsobj = ns->ToObject(env.local()).ToLocalChecked();

    auto testVal = nsobj->Get(env.local(), v8_str("exists")).ToLocalChecked();
    CHECK(testVal->IsTrue());

    testVal = nsobj->Get(env.local(), v8_str("test")).ToLocalChecked();
    CHECK(testVal->IsUndefined());

    testVal = nsobj->Get(env.local(), v8_str("toStringTag")).ToLocalChecked();
    CHECK(testVal.As<String>()->StrictEquals(v8_str("Module")));

    testVal = nsobj->Get(env.local(), v8_str("isExtensible")).ToLocalChecked();
    CHECK(testVal->IsFalse());

    testVal = nsobj->Get(env.local(), v8_str("proto")).ToLocalChecked();
    CHECK(testVal->IsUndefined());

    testVal = nsobj->Get(env.local(), v8_str("keys")).ToLocalChecked();
    CHECK(testVal.As<String>()->StrictEquals(v8_str("")));

    testVal = nsobj->Get(env.local(), v8_str("ownKeys")).ToLocalChecked();
    CHECK(testVal.As<String>()->StrictEquals(
        v8_str("Symbol(Symbol.toStringTag)")));

    testVal =
        nsobj->Get(env.local(), v8_str("definedProperty")).ToLocalChecked();
    CHECK(testVal->IsFalse());

    testVal =
        nsobj->Get(env.local(), v8_str("preventedExtensions")).ToLocalChecked();
    CHECK(testVal->IsTrue());

    testVal = nsobj->Get(env.local(), v8_str("ns")).ToLocalChecked();
    nsobj = testVal->ToObject(env.local()).ToLocalChecked();

    testVal = nsobj->Get(env.local(), v8_str("test")).ToLocalChecked();
    CHECK_EQ(10, testVal->Int32Value(env.local()).FromJust());
  }

  CHECK(!try_catch.HasCaught());
}

TEST(DynamicModuleUnknownExport) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  Local<v8::Object> ReferenceError =
      CompileRun("ReferenceError")->ToObject(env.local()).ToLocalChecked();

  dynamic = ScriptCompiler::CreateDynamicModule(isolate).ToLocalChecked();

  isolate->SetHostExecuteDynamicModuleCallback(
      HostExecuteDynamicModuleCallback);

  Local<String> source_text = v8_str("export { p } from 'dynamic'");
  ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
  ScriptCompiler::Source source(source_text, origin);
  Local<Module> module =
      ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  CHECK_EQ(module->GetStatus(), Module::kUninstantiated);
  CHECK(module->InstantiateModule(env.local(), ResolveCallbackDynamicModule)
            .FromJust());

  CHECK_EQ(Module::kInstantiated, module->GetStatus());
  CHECK_EQ(Module::kInstantiated, dynamic->GetStatus());

  CHECK(module->Evaluate(env.local()).IsEmpty());

  CHECK_EQ(Module::kErrored, module->GetStatus());
  CHECK_EQ(Module::kErrored, dynamic->GetStatus());

  CHECK(try_catch.HasCaught());
  // ExportNameUndefined
  CHECK(try_catch.Exception()
            ->InstanceOf(env.local(), ReferenceError)
            .FromJust());
}
}  // anonymous namespace
