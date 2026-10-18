function pack_mod(target, mod_define)
    import("lib.detect.find_file")

    local manifest_path = find_file("manifest.json", os.projectdir())
    if not manifest_path then
        cprint("${bright yellow}warn: ${reset}not found manifest.json in root dir!")
        return
    end
    local manifest = io.readfile(manifest_path)
    local outputdir = path.join(os.projectdir(), "bin", mod_define.modName)
    local targetfile = path.join(outputdir, mod_define.modFile)
    local oritargetfile = target:targetfile()
    local oripdbfile = path.join(path.directory(oritargetfile), path.basename(oritargetfile) .. ".pdb")

    os.mkdir(outputdir)
    os.cp(oritargetfile, targetfile)
    if os.isfile(oripdbfile) then
        os.cp(oripdbfile, path.join(outputdir, path.basename(mod_define.modFile) .. ".pdb"))
    end

    local formatted = manifest:gsub("%${(.-)}", function(var)
        return mod_define[var] or ("${" .. var .. "}")
    end)
    io.writefile(path.join(outputdir, "manifest.json"), formatted)
    cprint("${bright green}[mod Packer]: ${reset}mod already generated to " .. outputdir)
end

return { pack_mod = pack_mod }
