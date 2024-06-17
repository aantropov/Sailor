﻿using System.ComponentModel;
using YamlDotNet.RepresentationModel;
using YamlDotNet.Serialization.NamingConventions;
using YamlDotNet.Serialization;
using SailorEditor.Services;
using System.Numerics;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using SailorEngine;
using SailorEditor.Utility;
using YamlDotNet.Core;
using YamlDotNet.Core.Events;
using YamlDotNet.Core.Tokens;
using Scalar = YamlDotNet.Core.Events.Scalar;
using System;

namespace SailorEditor.ViewModels
{
    public partial class Uniform<T> : ObservableObject, ICloneable
    where T : IComparable<T>
    {
        public object Clone()
        {
            var res = new Uniform<T> { Key = Key };

            if (Value is ICloneable cloneable)
                res.Value = (T)cloneable.Clone();
            else
                res.Value = Value;

            return res;
        }

        public override bool Equals(object obj) => obj is Uniform<T> other ? Key.CompareTo(other.Key) == 0 : false;

        public override string ToString() => $"{Key.ToString()}: {Value.ToString()}";

        public override int GetHashCode() => Key?.GetHashCode() ?? 0;

        private void Value_PropertyChanged(object sender, PropertyChangedEventArgs e) => OnPropertyChanged(nameof(Value));

        public T Value
        {
            get => _value;
            set
            {
                if (!Equals(_value, value))
                {
                    if (_value != null)
                    {
                        if (_value is INotifyPropertyChanged propChanged)
                            propChanged.PropertyChanged -= Value_PropertyChanged;
                    }

                    SetProperty(ref _value, value, nameof(Value));

                    if (_value != null)
                    {
                        if (_value is INotifyPropertyChanged propChanged)
                            propChanged.PropertyChanged += Value_PropertyChanged;
                    }
                }
            }
        }

        [ObservableProperty]
        string key;

        T _value;
    }

    public class UniformYamlConverter<T> : IYamlTypeConverter where T : IComparable<T>
    {
        public UniformYamlConverter(IYamlTypeConverter[] ValueConverters = null)
        {
            if (ValueConverters != null)
                valueConverters = [.. ValueConverters];
        }

        public bool Accepts(Type type) => type == typeof(Uniform<T>);

        public object ReadYaml(IParser parser, Type type)
        {
            var deserializerBuilder = new DeserializerBuilder()
                .WithNamingConvention(CamelCaseNamingConvention.Instance);

            foreach (var valueConverter in valueConverters)
            {
                deserializerBuilder.WithTypeConverter(valueConverter);
            }

            var deserializer = deserializerBuilder.Build();

            var key = parser.Consume<Scalar>().Value;
            var value = deserializer.Deserialize<T>(parser);
            var uniform = new Uniform<T> { Key = key, Value = value };

            return uniform;
        }

        public void WriteYaml(IEmitter emitter, object value, Type type)
        {
            var uniform = (Uniform<T>)value;
            var serializerBuilder = new SerializerBuilder()
                .WithNamingConvention(CamelCaseNamingConvention.Instance);

            foreach (var valueConverter in valueConverters)
            {
                serializerBuilder.WithTypeConverter(valueConverter);
            }

            var serializer = serializerBuilder.Build();

            emitter.Emit(new YamlDotNet.Core.Events.Scalar(null, uniform.Key));
            serializer.Serialize(emitter, uniform.Value);
        }

        List<IYamlTypeConverter> valueConverters = [];
    }

    public partial class MaterialFile : AssetFile
    {
        [ObservableProperty]
        string renderQueue = "Opaque";

        [ObservableProperty]
        float depthBias = 0.0f;

        [ObservableProperty]
        bool supportMultisampling = true;

        [ObservableProperty]
        bool customDepthShader = true;

        [ObservableProperty]
        bool enableDepthTest = true;

        [ObservableProperty]
        bool enableZWrite = true;

        [ObservableProperty]
        string cullMode;

        [ObservableProperty]
        string blendMode;

        [ObservableProperty]
        string fillMode;

        [ObservableProperty]
        FileId shader;

        [ObservableProperty]
        ObservableList<Uniform<FileId>> samplers = [];

        [ObservableProperty]
        ObservableList<Uniform<Vec4>> uniformsVec4 = [];

        [ObservableProperty]
        ObservableList<Uniform<float>> uniformsFloat = [];

        [ObservableProperty]
        ObservableList<Observable<string>> shaderDefines = [];

        public override async Task<bool> LoadDependentResources()
        {
            if (!IsLoaded)
            {
                try
                {
                    var AssetService = MauiProgram.GetService<AssetsService>();
                    var preloadTasks = new List<Task>();
                    foreach (var tex in Samplers)
                    {
                        var task = Task.Run(async () =>
                        {
                            var file = AssetService.Files.Find((el) => el.FileId == tex.Value.Value);
                            if (file != null)
                            {
                                await file.LoadDependentResources();
                            }
                        });

                        preloadTasks.Add(task);
                    }

                    await Task.WhenAll(preloadTasks);
                }
                catch (Exception ex)
                {
                    DisplayName = ex.Message;
                }

                IsLoaded = true;
            }

            return true;
        }

        public override async Task Save()
        {
            using (var yamlAsset = new FileStream(Asset.FullName, FileMode.Create))
            using (var writer = new StreamWriter(yamlAsset))
            {
                var serializer = new SerializerBuilder()
                    .WithNamingConvention(CamelCaseNamingConvention.Instance)
                    .WithTypeConverter(new MaterialFileYamlConverter())
                    .Build();

                var yaml = serializer.Serialize(this);
                writer.Write(yaml);
            }

            IsDirty = false;
        }

        public override async Task Revert()
        {
            try
            {
                // Asset Info
                var yamlAssetInfo = File.ReadAllText(AssetInfo.FullName);

                var deserializerAssetInfo = new DeserializerBuilder()
                .WithNamingConvention(CamelCaseNamingConvention.Instance)
                .WithTypeConverter(new AssetFileYamlConverter())
                .IgnoreUnmatchedProperties()
                .Build();

                var intermediateObjectAssetInfo = deserializerAssetInfo.Deserialize<AssetFile>(yamlAssetInfo);
                FileId = intermediateObjectAssetInfo.FileId;
                Filename = intermediateObjectAssetInfo.Filename;

                // Asset
                var yamlAsset = File.ReadAllText(Asset.FullName);

                var deserializer = new DeserializerBuilder()
                .WithNamingConvention(CamelCaseNamingConvention.Instance)
                .WithTypeConverter(new MaterialFileYamlConverter())
                .IgnoreUnmatchedProperties()
                .Build();

                var intermediateObject = deserializer.Deserialize<MaterialFile>(yamlAsset);

                RenderQueue = intermediateObject.RenderQueue;
                DepthBias = intermediateObject.DepthBias;
                SupportMultisampling = intermediateObject.SupportMultisampling;
                CustomDepthShader = intermediateObject.CustomDepthShader;
                EnableDepthTest = intermediateObject.EnableDepthTest;
                EnableZWrite = intermediateObject.EnableZWrite;
                CullMode = intermediateObject.CullMode;
                BlendMode = intermediateObject.BlendMode;
                FillMode = intermediateObject.FillMode;
                Shader = intermediateObject.Shader;
                Samplers = intermediateObject.Samplers;
                UniformsVec4 = intermediateObject.UniformsVec4;
                UniformsFloat = intermediateObject.UniformsFloat;
                ShaderDefines = intermediateObject.ShaderDefines;

                DisplayName = Asset.Name;

                ShaderDefines.CollectionChanged += (a, e) => MarkDirty(nameof(ShaderDefines));
                ShaderDefines.ItemChanged += (a, e) => MarkDirty(nameof(ShaderDefines));

                UniformsFloat.CollectionChanged += (a, e) => MarkDirty(nameof(UniformsFloat));
                UniformsFloat.ItemChanged += (a, e) => MarkDirty(nameof(UniformsFloat));

                UniformsVec4.CollectionChanged += (a, e) => MarkDirty(nameof(UniformsVec4));
                UniformsVec4.ItemChanged += (a, e) => MarkDirty(nameof(UniformsVec4));

                Samplers.CollectionChanged += (a, e) => MarkDirty(nameof(Samplers));
                Samplers.ItemChanged += (a, e) => MarkDirty(nameof(Samplers));

                IsLoaded = false;
            }
            catch (Exception ex)
            {
                DisplayName = ex.Message;
            }

            IsDirty = false;
        }
    }

    public class MaterialFileYamlConverter : IYamlTypeConverter
    {
        public bool Accepts(Type type) => type == typeof(MaterialFile);

        public object ReadYaml(IParser parser, Type type)
        {
            var deserializer = new DeserializerBuilder()
                .WithNamingConvention(CamelCaseNamingConvention.Instance)
                .WithTypeConverter(new FileIdYamlConverter())
                .WithTypeConverter(new Vec4YamlConverter())
                .WithTypeConverter(new ObservableListConverter<Observable<FileId>>([new FileIdYamlConverter(), new ObservableObjectYamlConverter<FileId>()]))
                .WithTypeConverter(new ObservableListConverter<Uniform<FileId>>([new FileIdYamlConverter(), new UniformYamlConverter<FileId>()]))
                .WithTypeConverter(new ObservableListConverter<Uniform<Vec4>>([new Vec4YamlConverter(), new UniformYamlConverter<Vec4>()]))
                .WithTypeConverter(new ObservableListConverter<Uniform<float>>([new UniformYamlConverter<float>()]))
                .WithTypeConverter(new ObservableListConverter<Observable<string>>([new ObservableObjectYamlConverter<string>()]))
                .IgnoreUnmatchedProperties()
                .Build();

            var assetFile = new MaterialFile();

            parser.Consume<MappingStart>();

            while (parser.Current is not MappingEnd)
            {
                if (parser.Current is Scalar scalar)
                {
                    var propertyName = scalar.Value;
                    parser.MoveNext(); // Move to the value

                    switch (propertyName)
                    {
                        case "renderQueue":
                            assetFile.RenderQueue = deserializer.Deserialize<string>(parser);
                            break;
                        case "depthBias":
                            assetFile.DepthBias = deserializer.Deserialize<float>(parser);
                            break;
                        case "bSupportMultisampling":
                            assetFile.SupportMultisampling = deserializer.Deserialize<bool>(parser);
                            break;
                        case "bCustomDepthShader":
                            assetFile.CustomDepthShader = deserializer.Deserialize<bool>(parser);
                            break;
                        case "bEnableDepthTest":
                            assetFile.EnableDepthTest = deserializer.Deserialize<bool>(parser);
                            break;
                        case "bEnableZWrite":
                            assetFile.EnableZWrite = deserializer.Deserialize<bool>(parser);
                            break;
                        case "cullMode":
                            assetFile.CullMode = deserializer.Deserialize<string>(parser);
                            break;
                        case "blendMode":
                            assetFile.BlendMode = deserializer.Deserialize<string>(parser);
                            break;
                        case "fillMode":
                            assetFile.FillMode = deserializer.Deserialize<string>(parser);
                            break;
                        case "shaderUid":
                            assetFile.Shader = deserializer.Deserialize<FileId>(parser);
                            break;
                        case "samplers":
                            var samplers = deserializer.Deserialize<Dictionary<string, FileId>>(parser) ?? new();
                            assetFile.Samplers = new ObservableList<Uniform<FileId>>(samplers.Select(s => new Uniform<FileId> { Key = s.Key, Value = s.Value }).ToList());
                            break;
                        case "uniformsVec4":
                            var uniformsVec4 = deserializer.Deserialize<Dictionary<string, Vec4>>(parser) ?? new();
                            assetFile.UniformsVec4 = new ObservableList<Uniform<Vec4>>(uniformsVec4.Select(s => new Uniform<Vec4> { Key = s.Key, Value = s.Value }).ToList());
                            break;
                        case "uniformsFloat":
                            var uniformsFloat = deserializer.Deserialize<Dictionary<string, float>>(parser) ?? new();
                            assetFile.UniformsFloat = new ObservableList<Uniform<float>>(uniformsFloat.Select(s => new Uniform<float> { Key = s.Key, Value = s.Value }).ToList());
                            break;
                        case "defines":
                            assetFile.ShaderDefines = deserializer.Deserialize<ObservableList<Observable<string>>>(parser);
                            break;
                        default:
                            deserializer.Deserialize<object>(parser);
                            break;
                    }
                }
                else
                {
                    parser.MoveNext();
                }
            }

            parser.Consume<MappingEnd>();

            return assetFile;
        }

        public void WriteYaml(IEmitter emitter, object value, Type type)
        {
            var assetFile = (MaterialFile)value;

            emitter.Emit(new MappingStart(null, null, false, MappingStyle.Block));

            // Serialize MaterialFile specific fields
            emitter.Emit(new Scalar(null, "renderQueue"));
            emitter.Emit(new Scalar(null, assetFile.RenderQueue));

            emitter.Emit(new Scalar(null, "depthBias"));
            emitter.Emit(new Scalar(null, assetFile.DepthBias.ToString()));

            emitter.Emit(new Scalar(null, "bSupportMultisampling"));
            emitter.Emit(new Scalar(null, assetFile.SupportMultisampling.ToString().ToLower()));

            emitter.Emit(new Scalar(null, "bCustomDepthShader"));
            emitter.Emit(new Scalar(null, assetFile.CustomDepthShader.ToString().ToLower()));

            emitter.Emit(new Scalar(null, "bEnableDepthTest"));
            emitter.Emit(new Scalar(null, assetFile.EnableDepthTest.ToString().ToLower()));

            emitter.Emit(new Scalar(null, "bEnableZWrite"));
            emitter.Emit(new Scalar(null, assetFile.EnableZWrite.ToString().ToLower()));

            emitter.Emit(new Scalar(null, "cullMode"));
            emitter.Emit(new Scalar(null, assetFile.CullMode));

            emitter.Emit(new Scalar(null, "blendMode"));
            emitter.Emit(new Scalar(null, assetFile.BlendMode));

            emitter.Emit(new Scalar(null, "fillMode"));
            emitter.Emit(new Scalar(null, assetFile.FillMode));

            emitter.Emit(new Scalar(null, "shaderUid"));
            emitter.Emit(new Scalar(null, assetFile.Shader?.Value));

            emitter.Emit(new Scalar(null, "samplers"));
            emitter.Emit(new MappingStart(null, null, false, MappingStyle.Block));
            foreach (var sampler in assetFile.Samplers)
            {
                emitter.Emit(new Scalar(null, sampler.Key));
                emitter.Emit(new Scalar(null, sampler.Value.Value));
            }
            emitter.Emit(new MappingEnd());

            emitter.Emit(new Scalar(null, "uniformsVec4"));
            emitter.Emit(new MappingStart(null, null, false, MappingStyle.Block));
            foreach (var uniform in assetFile.UniformsVec4)
            {
                emitter.Emit(new Scalar(null, uniform.Key));
                emitter.Emit(new SequenceStart(null, null, false, SequenceStyle.Block));
                emitter.Emit(new Scalar(null, uniform.Value.X.ToString()));
                emitter.Emit(new Scalar(null, uniform.Value.Y.ToString()));
                emitter.Emit(new Scalar(null, uniform.Value.Z.ToString()));
                emitter.Emit(new Scalar(null, uniform.Value.W.ToString()));
                emitter.Emit(new SequenceEnd());
            }
            emitter.Emit(new MappingEnd());

            emitter.Emit(new Scalar(null, "uniformsFloat"));
            emitter.Emit(new MappingStart(null, null, false, MappingStyle.Block));
            foreach (var uniform in assetFile.UniformsFloat)
            {
                emitter.Emit(new Scalar(null, uniform.Key));
                emitter.Emit(new Scalar(null, uniform.Value.ToString()));
            }
            emitter.Emit(new MappingEnd());

            emitter.Emit(new Scalar(null, "defines"));
            emitter.Emit(new SequenceStart(null, null, false, SequenceStyle.Block));
            foreach (var define in assetFile.ShaderDefines)
            {
                emitter.Emit(new Scalar(null, define.Value));
            }
            emitter.Emit(new SequenceEnd());

            emitter.Emit(new MappingEnd());
        }
    }
}