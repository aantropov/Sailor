﻿using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using YamlDotNet.Core.Events;
using YamlDotNet.Core;
using YamlDotNet.Serialization.NamingConventions;
using YamlDotNet.Serialization;
using YamlDotNet.RepresentationModel;

namespace SailorEditor.Utility
{
    public sealed class ObservableList<T> : ObservableCollection<T>, ICollectionItemPropertyChanged<T>
        where T : INotifyPropertyChanged
    {
        public event EventHandler<ItemChangedEventArgs<T>> ItemChanged;

        public ObservableList()
        {
            CollectionChanged += FullObservableCollectionCollectionChanged;
        }

        public ObservableList(IEnumerable<T> pItems) : this()
        {
            foreach (var item in pItems)
                this.Add(item);
        }

        private void FullObservableCollectionCollectionChanged(object sender, NotifyCollectionChangedEventArgs e)
        {
            if (e.NewItems != null)
            {
                foreach (Object item in e.NewItems)
                {
                    (item as INotifyPropertyChanged).PropertyChanged += ItemPropertyChanged;
                }
            }
            if (e.OldItems != null)
            {
                foreach (Object item in e.OldItems)
                {
                    (item as INotifyPropertyChanged).PropertyChanged -= ItemPropertyChanged;
                }
            }
        }

        private void ItemPropertyChanged(object sender, PropertyChangedEventArgs e)
        {
            var args = new ItemChangedEventArgs<T>((T)sender, e.PropertyName);
            this.ItemChanged?.Invoke(this, args);
        }
    }

    internal interface ICollectionItemPropertyChanged<T>
    {
        event EventHandler<ItemChangedEventArgs<T>> ItemChanged;
    }

    public class ItemChangedEventArgs<T>
    {
        public T ChangedItem { get; }
        public string PropertyName { get; }

        public ItemChangedEventArgs(T item, string propertyName)
        {
            ChangedItem = item;
            PropertyName = propertyName;
        }
    }

    public class ObservableListConverter<T> : IYamlTypeConverter
        where T : INotifyPropertyChanged
    {
        public ObservableListConverter(IYamlTypeConverter[] ValueConverters = null)
        {
            if (ValueConverters != null)
                valueConverters = [.. ValueConverters];
        }

        public bool Accepts(Type type) => type == typeof(ObservableList<T>);

        public object ReadYaml(IParser parser, Type type)
        {
            var deserializerBuilder = new DeserializerBuilder()
                .WithNamingConvention(CamelCaseNamingConvention.Instance)
                .IgnoreUnmatchedProperties()
                .IncludeNonPublicProperties();

            foreach (var valueConverter in valueConverters)
            {
                deserializerBuilder.WithTypeConverter(valueConverter);
            }

            var deserializer = deserializerBuilder.Build();

            var list = new ObservableList<T>();
            if (parser.Current is SequenceStart)
            {
                parser.MoveNext();
                while (parser.Current is not SequenceEnd)
                {
                    var item = deserializer.Deserialize<T>(parser);
                    list.Add(item);
                }
                parser.MoveNext();
            }
            else
            {
                parser.MoveNext();
            }

            return list;
        }

        public void WriteYaml(IEmitter emitter, object value, Type type)
        {
            var list = (ObservableList<T>)value;
            var serializerBuilder = new SerializerBuilder()
                .WithNamingConvention(CamelCaseNamingConvention.Instance);

            valueConverters.ForEach((el) => serializerBuilder.WithTypeConverter(el));

            var serializer = serializerBuilder.Build();

            emitter.Emit(new SequenceStart(null, null, false, SequenceStyle.Block));

            foreach (var item in list)
            {
                string itemYaml = serializer.Serialize(item);
                
                var yaml = new YamlStream();
                using (var sr = new StringReader(itemYaml))
                {
                    yaml.Load(sr);
                }

                var doc = yaml.Documents[0].RootNode;
                EmitNode(emitter, doc);
            }

            emitter.Emit(new SequenceEnd());
        }

        private void EmitNode(IEmitter emitter, YamlNode node)
        {
            switch (node)
            {
                case YamlScalarNode scalar:
                    emitter.Emit(new Scalar(null, null, scalar.Value, ScalarStyle.Any, true, false));
                    break;
                case YamlMappingNode mapping:
                    emitter.Emit(new MappingStart(null, null, false, MappingStyle.Block));
                    foreach (var kvp in mapping.Children)
                    {
                        EmitNode(emitter, kvp.Key);
                        EmitNode(emitter, kvp.Value);
                    }
                    emitter.Emit(new MappingEnd());
                    break;
                case YamlSequenceNode sequence:
                    emitter.Emit(new SequenceStart(null, null, false, SequenceStyle.Block));
                    foreach (var child in sequence.Children)
                    {
                        EmitNode(emitter, child);
                    }
                    emitter.Emit(new SequenceEnd());
                    break;
            }
        }

        List<IYamlTypeConverter> valueConverters = [];
    }
}
