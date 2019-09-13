import fs from "fs";
import https from "https";
import nunjucks from "nunjucks";

import pkg from "../package.json";

import {
  warn,
  getPlatform,
  getCamelizedName,
  getSnakeCaseName
} from "./utils.mjs";

import {
  getASTType,
  getASTCategoryByName,
  getDawnDeclarationName,
  getExplortDeclarationName
} from "./types.mjs";

import generateGyp from "./generators/gyp.mjs";
import generateIndex from "./generators/index.mjs";
import generateMemoryLayouts from "./generators/memoryLayouts.mjs";

const GEN_FILE_NOTICE = `/*
 * MACHINE GENERATED, DO NOT EDIT
 * GENERATED BY ${pkg.name} v${pkg.version}
 */
`;

const dawnVersion = process.env.npm_config_dawnversion;
if (!dawnVersion) throw `No Dawn version --dawnversion specified!`;

// dst write paths
const baseGeneratePath = pkg.config.GEN_OUT_DIR;
const generateVersionPath = `${baseGeneratePath}/${dawnVersion}`;
const generatePath = `${generateVersionPath}/${getPlatform()}`;
const generateSrcPath = `${generatePath}/src`;

// enables js interface minifcation
const enableMinification = false;

// indicating if it's necessary to include memorylayouts in the build
const includeMemoryLayouts = !fs.existsSync(`${generatePath}/memoryLayouts.json`);

function writeGeneratedFile(path, text, includeNotice = true) {
  if (typeof text !== "string") throw new TypeError(`Expected 'string' type for parameter 'text'`);
  // append notice
  if (includeNotice) text = GEN_FILE_NOTICE + text;
  fs.writeFileSync(path, text);
};

function generateAST(ast) {
  let out = {};
  // normalize
  {
    let normalized = [];
    for (let key in ast) {
      if (!ast.hasOwnProperty(key)) continue;
      if (key === "_comment") continue;
      normalized.push({
        textName: key,
        ...ast[key]
      });
    };
    // overwrite input with normalized input
    ast = normalized;
  }
  // generate enum nodes
  {
    let enums = ast.filter(node => {
      return node.category === "enum";
    });
    enums = enums.map(enu => {
      let node = {};
      let {textName} = enu;
      node.name = getDawnDeclarationName(textName);
      node.externalName = getExplortDeclarationName(node.name);
      node.type = getASTCategoryByName(textName, ast);
      node.textName = textName;
      node.children = [];
      enu.values.map(member => {
        let {value} = member;
        let name = getSnakeCaseName(member.name);
        let type = {
          isString: true,
          nativeType: "char",
          jsType: { isString: true, type: "String" }
        };
        let child = {
          name,
          type,
          value
        };
        node.children.push(child);
      });
      return node;
    });
    out.enums = enums;
  }
  // generate bitmask nodes
  {
    let bitmasks = ast.filter(node => {
      return node.category === "bitmask";
    });
    bitmasks = bitmasks.map(bitmask => {
      let node = {};
      let {textName} = bitmask;
      node.name = getDawnDeclarationName(textName);
      node.externalName = getExplortDeclarationName(node.name);
      node.type = getASTCategoryByName(textName, ast);
      node.textName = textName;
      node.children = [];
      bitmask.values.map(member => {
        let {value} = member;
        let name = getSnakeCaseName(member.name);
        let type = {
          isNumber: true,
          nativeType: "uint32_t",
          jsType: { isNumber: true, type: "Number" }
        };
        let child = {
          name,
          type,
          value
        };
        node.children.push(child);
      });
      return node;
    });
    out.bitmasks = bitmasks;
  }
  // generate object nodes
  {
    let objects = ast.filter(node => {
      return node.category === "object";
    });
    objects = objects.map(object => {
      let node = {};
      let {textName} = object;
      node.name = getDawnDeclarationName(textName);
      node.externalName = getExplortDeclarationName(node.name);
      node.type = getASTCategoryByName(textName, ast);
      node.textName = textName;
      node.children = [];
      // process the object's methods
      (object.methods || []).map(method => {
        let name = getCamelizedName(method.name);
        let child = {
          name,
          children: []
        };
        if (method.returns) {
          child.type = getASTType({ type: method.returns }, ast);
        } else {
          child.type = getASTType({ type: "void" }, ast);
        }
        // process the method's arguments
        (method.args || []).map(arg => {
          let name = getCamelizedName(arg.name);
          let type = getASTType(arg, ast);
          let argChild = {
            name,
            type
          };
          if (arg.optional) argChild.isOptional = true;
          child.children.push(argChild);
        });
        node.children.push(child);
      });
      return node;
    });
    out.objects = objects;
  }
  // generate structure nodes
  {
    let structures = ast.filter(node => {
      return node.category === "structure";
    });
    structures = structures.map(structure => {
      let node = {};
      let {textName} = structure;
      node.name = getDawnDeclarationName(textName);
      node.externalName = getExplortDeclarationName(node.name);
      node.type = getASTCategoryByName(textName, ast);
      node.textName = textName;
      node.children = [];
      structure.members.map(member => {
        let name = getCamelizedName(member.name);
        let type = getASTType(member, ast);
        let child = {
          name,
          type
        };
        if (member.optional) child.isOptional = true;
        node.children.push(child);
      });
      return node;
    });
    out.structures = structures;
  }
  return out;
};

async function generateBindings(version, enableMinification, includeMemoryLayouts) {
  let JSONspecification = fs.readFileSync(pkg.config.SPEC_DIR + `/${version}.json`, "utf-8");
  let fakePlatform = process.env.npm_config_fake_platform;
  // let the user know when he uses a fake platform
  if (fakePlatform) {
    console.log(`Fake platform enabled!`);
    console.log(`Fake platform: '${fakePlatform}' - Real platform: '${process.platform}'`);
  }
  if (!enableMinification) console.log(`Code minification is disabled!`);
  if (includeMemoryLayouts) console.log(`Memory layouts are not inlined yet.`);
  // reserve dst write paths
  {
    // generated/
    if (!fs.existsSync(baseGeneratePath)) fs.mkdirSync(baseGeneratePath);
    // generated/version/
    if (!fs.existsSync(generateVersionPath)) fs.mkdirSync(generateVersionPath);
    // generated/version/platform/
    if (!fs.existsSync(generatePath)) fs.mkdirSync(generatePath);
    // generated/version/platform/src/
    if (!fs.existsSync(generateSrcPath)) fs.mkdirSync(generateSrcPath);
  }
  console.log(`Generating bindings for ${version}...`);
  let ast = generateAST(JSON.parse(JSONspecification));
  // generate AST
  {
    let out = JSON.stringify(ast, null, 2);
    // .json
    writeGeneratedFile(`${generatePath}/ast.json`, out, false);
  }
  // generate gyp
  {
    let out = generateGyp(ast);
    // .gyp
    writeGeneratedFile(`${generatePath}/binding.gyp`, out.gyp, false);
  }
  // generate index
  {
    let out = generateIndex(ast, includeMemoryLayouts);
    // .h
    writeGeneratedFile(`${generatePath}/src/index.h`, out.header);
    // .cpp
    writeGeneratedFile(`${generatePath}/src/index.cpp`, out.source);
  }
  // generate memorylayouts
  {
    let out = generateMemoryLayouts(ast);
    // .h
    writeGeneratedFile(`${generatePath}/src/memoryLayouts.h`, out.header);
  }
  console.log(`Successfully generated bindings!`);
};

generateBindings(
  dawnVersion,
  enableMinification,
  includeMemoryLayouts
);
